#!/usr/bin/env python3
"""Guided Bluetooth test at the van.

Reads the hub's listen-only survey log from the USB port, saves all of it to a
file, and walks you through a list of steps ("turn pump 1 on and off 3 times").
Each step is marked in the log, and while you do it the screen shows which
Bluetooth devices changed their data, so you can see straight away whether the
BMPRO or heater reacted.

Run it instead of `pio device monitor` (only one program can open the port):

    guided-survey.bat                 (in firmware\\hub\\tools, uses COM4)
    python guided_survey.py --port COM5

Keys during a step:  Enter = step done,  s = skip,  l = set up later,  q = finish now.
"Set up later" (e.g. the gas sensor isn't fitted yet) is remembered in
guided-later.txt and that step is left out of future runs. Run just those
steps once the part is fitted:  guided-survey.bat COM4 --later
Needs pyserial (PlatformIO's own Python already has it).
"""
import argparse
import json
import datetime as dt
import os
import re
import sys
import threading
import time
from collections import defaultdict

STEPS = [
    ("baseline", "Don't touch anything for 60 seconds. This is the 'nothing happening' baseline.", 60),
    ("pump1", "Turn PUMP 1 (internal) ON and OFF 3 times, about 5 seconds each way. Use the Trek 3 screen or its switch.", 0),
    ("pump2", "Turn PUMP 2 (external) ON and OFF 3 times, about 5 seconds each way.", 0),
    ("tap", "Turn pump 1 on and run a tap for about 30 seconds (so a tank level can move), then turn the pump off.", 0),
    ("lights", "Switch a big 12 V load on and off 3 times (lights, fan, or the fridge if easy), 5 seconds each way.", 0),
    ("heater_on", "Turn the DIESEL HEATER ON with its own remote or app. Leave it running.", 0),
    ("heater_temp", "Change the heater's set temperature up 3 degrees, then back down 3, a few seconds apart.", 0),
    ("heater_off", "Turn the heater OFF (it will keep its fan going while it cools down - that's fine).", 0),
    ("bmpro_app", "Open the BMPRO app on your phone, connect, look at battery and tanks for 30 s, then close it.", 0),
    ("gas", "If the gas bottle sensor is fitted: lift it off the bottle for 10 seconds and put it back.", 0),
    ("quiet", "Last one: don't touch anything for 60 seconds.", 60),
]

ADV = re.compile(r"^ADV \d+ (?P<mac>[0-9a-f:]{17}) rssi=(?P<rssi>-?\d+) tag=(?P<tag>\S+)"
                 r"(?: name=\"(?P<name>[^\"]*)\")?.* raw=(?P<raw>[0-9a-f]*)")


class Tracker:
    """Counts payload changes per device, overall and inside the current step."""

    def __init__(self):
        self.lock = threading.Lock()
        self.info = {}  # mac -> (tag, name, rssi)
        self.total = defaultdict(int)
        self.step_counts = defaultdict(int)
        self.step = None
        self.step_start = 0.0
        self.first_seen = {}
        self.live = True

    def advert(self, m):
        mac = m["mac"]
        with self.lock:
            old = self.info.get(mac)
            name = m["name"] or (old[1] if old else "")
            self.info[mac] = (m["tag"], name, int(m["rssi"]))
            self.first_seen.setdefault(mac, time.time())
            self.total[mac] += 1
            if self.step:
                self.step_counts[mac] += 1
                n = self.step_counts[mac]
                # Devices that are normally quiet: show every change (that's
                # what a pump switch looks like). Chatty ones: just now and then.
                before = self.total[mac] - n
                age = max(1.0, self.step_start - self.first_seen[mac])
                quiet = before / age < 0.1
                if self.live and (quiet or n in (1, 5, 10, 20, 50)):
                    print(f"   change #{n:<3} {label(mac, self.info[mac])}" + ("" if quiet else "  (always busy)"))

    def begin(self, step):
        with self.lock:
            self.step = step
            self.step_start = time.time()
            self.step_counts = defaultdict(int)

    def end(self):
        with self.lock:
            secs = max(1.0, time.time() - self.step_start)
            counts = dict(self.step_counts)
            self.step = None
            elapsed_total = {mac: max(1.0, time.time() - self.first_seen[mac]) for mac in counts}
        # Rank by how much busier each device was during the step than usual.
        rows = []
        for mac, n in counts.items():
            usual = self.total[mac] / elapsed_total[mac] * secs
            rows.append((n / max(usual, 0.5), n, mac))
        rows.sort(reverse=True)
        return secs, rows[:6]


def label(mac, info):
    tag, name, rssi = info
    s = f"{mac}  tag={tag:<14} rssi={rssi:<4}"
    return s + (f' "{name}"' if name else "")


def reader(ser, logf, tracker, stop):
    buf = b""
    while not stop.is_set():
        try:
            chunk = ser.read(512)
        except Exception as e:  # unplugged
            print(f"\n!! lost the hub's USB port: {e}")
            stop.set()
            return
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            stamp = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            logf.write(f"{stamp} {line}\n")
            logf.flush()
            m = ADV.match(line)
            if m:
                tracker.advert(m)
            elif line.startswith("[victron]") or line.startswith("[survey]") and "BLUETOOTH SURVEY" in line:
                print("   " + line)


def mark(logf, text):
    stamp = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    logf.write(f"{stamp} ===== {text}\n")
    logf.flush()


def wait_step(secs, stop):
    """Timed step: count down. Otherwise wait for Enter / s / q."""
    if secs:
        end = time.time() + secs
        while time.time() < end and not stop.is_set():
            time.sleep(0.5)
        return "done"
    try:
        ans = input().strip().lower()
    except EOFError:
        return "quit"
    return {"s": "skip", "q": "quit", "l": "later"}.get(ans[:1], "done")


LATER_FILE = "guided-later.txt"


def load_later():
    try:
        with open(LATER_FILE, encoding="utf-8") as f:
            return set(json.load(f))
    except (OSError, ValueError):
        return set()


def save_later(keys):
    with open(LATER_FILE, "w", encoding="utf-8") as f:
        json.dump(sorted(keys), f)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", default=None, help="log file (default: van-test-<date-time>.log here)")
    ap.add_argument("--warmup", type=int, default=30, help="seconds of listening before the first step")
    ap.add_argument("--later", action="store_true", help="run only the steps marked 'set up later'")
    args = ap.parse_args()
    later = load_later()
    if args.later:
        steps = [st for st in STEPS if st[0] in later]
        if not steps:
            sys.exit("No steps are marked 'set up later'.")
    else:
        steps = [st for st in STEPS if st[0] not in later]
        if later:
            print("Left out (set up later): " + ", ".join(sorted(later)) + "   - run them with --later")

    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("pyserial missing: run this with PlatformIO's Python (guided-survey.bat does that).")

    out = args.out or dt.datetime.now().strftime("van-test-%Y%m%d-%H%M.log")
    try:
        ser = serial.serial_for_url(args.port, args.baud, timeout=0.2)
    except Exception as e:
        sys.exit(f"Can't open {args.port}: {e}\nClose any other serial monitor (pio device monitor) and try again.")

    tracker = Tracker()
    stop = threading.Event()
    with open(out, "w", encoding="utf-8") as logf:
        mark(logf, f"guided survey started, port {args.port}")
        th = threading.Thread(target=reader, args=(ser, logf, tracker, stop), daemon=True)
        th.start()
        print(f"Saving everything to {os.path.abspath(out)}")
        print(f"Listening for {args.warmup} s to learn what's around before the first step...")
        tracker.live = False
        time.sleep(args.warmup)
        tracker.live = True

        for i, (key, text, secs) in enumerate(steps, 1):
            if stop.is_set():
                break
            print("\n" + "=" * 70)
            print(f"STEP {i}/{len(steps)}: {text}")
            print("   (wait {} s)".format(secs) if secs else
                  "   Press Enter when done, s to skip, l = set up later (not fitted yet), q to finish.")
            tracker.begin(key)
            mark(logf, f"STEP {key} START: {text}")
            res = wait_step(secs, stop)
            dur, top = tracker.end()
            mark(logf, f"STEP {key} {res.upper()} after {dur:.0f} s")
            if res == "quit":
                break
            if res == "later":
                later.add(key)
                save_later(later)
                print("   OK - left out from now on. Run it later with --later.")
                continue
            if res == "skip":
                continue
            if args.later and key in later:
                later.discard(key)  # done now: back in the normal list
                save_later(later)
            if top:
                print(f"\n   Most active during this step ({dur:.0f} s):")
                for ratio, n, mac in top:
                    hint = "  <-- busier than usual" if ratio >= 2 and n >= 2 else ""
                    print(f"   {n:>3} changes  {label(mac, tracker.info[mac])}{hint}")
            else:
                print("   No device changed its data during this step.")

        stop.set()
        th.join(timeout=1)
        mark(logf, "guided survey finished")
        logf.write("===== devices heard (total payload changes)\n")
        for mac, n in sorted(tracker.total.items(), key=lambda kv: -kv[1]):
            logf.write(f"===== {n:>5}  {label(mac, tracker.info[mac])}\n")
    ser.close()
    print(f"\nAll done. Please send this file: {os.path.abspath(out)}")


if __name__ == "__main__":
    main()
