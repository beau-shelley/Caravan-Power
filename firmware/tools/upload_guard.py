# PlatformIO extra script: refuse to flash the wrong board.
#
# 2026-11-26: the display env's custom_sdkconfig makes pioarduino re-run itself,
# and the inner run dropped --upload-port and auto-detected the hub (COM5): the
# display firmware went onto the hub twice and the hub went silent.
#
# Set in the env:
#   upload_port = COM3                       ; never auto-detect
#   custom_expected_mac = 44:1b:f6:95:36:28  ; optional: the board this env is for
# Before every upload this reads the MAC on the upload port (esptool read_mac)
# and stops the upload if it isn't the expected board.
# Skip it with:  set UPLOAD_GUARD=0
import os
import re
import subprocess
import sys

Import("env")  # noqa: F821  (provided by PlatformIO)


def _guard(source, target, env):  # noqa: ARG001
    if os.environ.get("UPLOAD_GUARD") == "0":
        return
    want = (env.GetProjectOption("custom_expected_mac", "") or "").strip().lower()
    port = env.subst("$UPLOAD_PORT")
    if not port:
        sys.stderr.write("upload_guard: no upload_port set for this env - refusing to auto-detect a board\n")
        env.Exit(1)
    if not want:
        print(f"upload_guard: uploading to {port} (no custom_expected_mac set)")
        return
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy") or "", "esptool.py")
    cmd = [env.subst("$PYTHONEXE"), esptool, "--port", port, "read_mac"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"upload_guard: could not read the MAC on {port}: {e}\n")
        env.Exit(1)
    macs = re.findall(r"mac:\s*([0-9a-f:]{17})", out.lower())
    if not macs:
        sys.stderr.write(f"upload_guard: no MAC read on {port}:\n{out}\n")
        env.Exit(1)
    if macs[-1] != want:
        sys.stderr.write(f"upload_guard: {port} is {macs[-1]}, not {want} - wrong board, upload stopped\n")
        env.Exit(1)
    print(f"upload_guard: {port} is {want} - OK")


env.AddPreAction("upload", _guard)  # noqa: F821
