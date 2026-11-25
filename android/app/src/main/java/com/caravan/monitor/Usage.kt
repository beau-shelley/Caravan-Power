package com.caravan.monitor

import kotlin.math.roundToInt

// Worked out on the phone from the readings it receives, so the hub does no extra work.
// The only history kept is the gas bottle % every 10 minutes (8 days, a few KB), for
// the gas use rate. Pure Kotlin: no Android, unit-tested.

/** Gas bottle % last seen in one 10-minute slice. */
data class Slice(val start: Long, val gas: Double) {
    fun toLine() = "$start,${String.format(java.util.Locale.US, "%.1f", gas)}"

    companion object {
        const val SECONDS = 600L
        fun parse(line: String): Slice? = try {
            val p = line.split(',')
            // Older logs had more columns: the bottle % was the 5th.
            if (p.size >= 8) Slice(p[0].toLong(), p[4].toDouble()).takeIf { it.gas >= 0 }
            else Slice(p[0].toLong(), p[1].toDouble())
        } catch (_: Exception) { null }
    }
}

/** The friendly extras shown on Home. null = nothing to show (shown as "--" / left out). */
data class Outlook(val battery: String? = null, val gas: String? = null)

class UsageLog(private val maxSlices: Int = 8 * 144) {
    val slices = ArrayDeque<Slice>()
    var current: Slice? = null
        private set
    private var lastAtMs = 0L
    /** Battery current averaged over about 15 minutes, so the fridge cycling doesn't make the time jump about. */
    var avgA = Double.NaN
        private set

    /** A fresh (live) state at wall-clock [nowMs]. Returns true when a slice was finished (save the log then). */
    fun add(st: HubState, nowMs: Long): Boolean {
        val dtS = if (lastAtMs > 0) (nowMs - lastAtMs) / 1000.0 else -1.0
        lastAtMs = nowMs
        val b = st.bat
        avgA = when {
            !b.on -> Double.NaN
            avgA.isNaN() || dtS < 0 || dtS > 300 -> b.a
            else -> avgA + (b.a - avgA) * (dtS / AVG_TAU_S).coerceAtMost(1.0)
        }
        if (!st.gas.on) return false
        val key = (nowMs / 1000) / Slice.SECONDS * Slice.SECONDS
        val c = current
        current = Slice(key, st.gas.pct)
        if (c != null && c.start != key) { push(c); return true }
        return false
    }

    /** The link went quiet: the next state starts a new averaging run. */
    fun lost() { avgA = Double.NaN }

    private fun push(s: Slice) {
        slices.addLast(s)
        while (slices.size > maxSlices) slices.removeFirst()
    }

    fun load(lines: List<String>) {
        slices.clear()
        lines.mapNotNull { Slice.parse(it) }.sortedBy { it.start }.forEach { push(it) }
    }

    fun lines(): List<String> = slices.map { it.toLine() }

    /** Every slice, the one in progress included. */
    fun all(): List<Slice> = slices.toList() + listOfNotNull(current)

    fun outlook(st: HubState, meta: Meta, nowS: Long): Outlook =
        Outlook(battery = batteryText(st.bat, meta.batteryAh, avgA), gas = if (st.gas.on) gasShort(all(), st.gas.pct, nowS) else null)

    companion object {
        const val AVG_TAU_S = 900.0

        /**
         * "About 2 days left at this load", "Full in 1 h 20 min", "Idle". Discharging uses the
         * ~15 min average current against what's left in the bank ([capAh]); the hub's own
         * time-to-go (10 s average) is the fallback while the bank size isn't known.
         */
        fun batteryText(b: Battery, capAh: Int, avgA: Double): String? {
            if (!b.on) return null
            val a = if (avgA.isNaN()) b.a else avgA
            return when {
                a < -0.3 -> {
                    val min = if (capAh > 0) (b.soc / 100.0 * capAh / -a * 60).roundToInt() else b.ttgMin
                    if (min < 0) "Idle" else "${about(min)} left at this load"
                }
                b.a > 0.3 && b.ttgMin >= 0 -> "Full in ${hm(b.ttgMin)}"
                b.a > 0.3 -> "Charging"
                else -> "Idle"
            }
        }

        /** 45 min, 2 h 10 min, About 9 h, About 2 days, About 2½ days, Over 2 weeks. */
        fun about(min: Int): String = when {
            min < 180 -> hm(min)
            min < 36 * 60 -> "About ${(min / 60.0).roundToInt()} h"
            min > 14 * 1440 -> "Over 2 weeks"
            else -> {
                val halves = (min / 720.0).roundToInt()
                "About ${halves / 2}${if (halves % 2 == 1) "½" else ""} days"
            }
        }

        fun hm(min: Int) = if (min >= 60) "${min / 60} h ${min % 60} min" else "$min min"

        /**
         * Gas use per day from the bottle % over the last week (since the last refill), as a
         * straight-line fit. Gaps in what the phone saw don't skew it: it's the slope between
         * readings, not a total. null until the readings span at least 18 hours.
         */
        fun gasPerDay(slices: List<Slice>, nowS: Long): Double? {
            val pts = slices.filter { it.start >= nowS - 7 * 86400 }
            if (pts.isEmpty()) return null
            var from = 0
            for (i in 1 until pts.size) if (pts[i].gas > pts[i - 1].gas + 15) from = i  // bottle swapped / refilled
            val use = pts.subList(from, pts.size)
            if (use.last().start - use.first().start < 18 * 3600) return null
            val xs = use.map { (it.start - use.first().start) / 86400.0 }
            val ys = use.map { it.gas }
            val mx = xs.average(); val my = ys.average()
            val sxx = xs.sumOf { (it - mx) * (it - mx) }
            if (sxx <= 0) return null
            return -xs.indices.sumOf { (xs[it] - mx) * (ys[it] - my) } / sxx
        }

        /** Short form for the gas card: "12 days left", "Over 2 weeks", "Barely used", "Measuring". */
        fun gasShort(slices: List<Slice>, pct: Double, nowS: Long): String {
            val perDay = gasPerDay(slices, nowS) ?: return "Measuring"
            if (perDay < 0.5) return "Barely used"
            val t = about((pct / perDay * 1440).roundToInt())
            return if (t.startsWith("Over")) t else t.removePrefix("About ") + " left"
        }
    }
}
