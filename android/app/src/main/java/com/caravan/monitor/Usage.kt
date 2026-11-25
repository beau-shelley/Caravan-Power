package com.caravan.monitor

import java.util.Locale
import kotlin.math.abs
import kotlin.math.roundToInt

// Everything here is worked out on the phone from the readings it receives, so the hub
// does no extra work. The app keeps a small log of 10-minute slices (8 days, about 60 KB)
// while it's connected, locally or through the cloud. Pure Kotlin: no Android, unit-tested.

/** What the app saw in one 10-minute slice. Snapshot values are the last seen; -1 / NaN = none. */
data class Slice(
    val start: Long,                 // unix seconds, a multiple of 600
    val covered: Int = 0,            // seconds of the slice the app was receiving live data
    val loadAh: Double = 0.0,        // amp-hours drawn by the loads
    val soc: Double = -1.0,
    val gas: Double = -1.0,
    val tanks: List<Double> = emptyList(),
    val fridgeMin: Double = Double.NaN,
    val solarKwh: Double = -1.0,     // the hub's "kWh today" for all solar, last seen
) {
    fun toLine(): String = listOf(
        start, covered, f(loadAh), f(soc), f(gas), tanks.joinToString("|") { f(it) },
        if (fridgeMin.isNaN()) "" else f(fridgeMin), f(solarKwh),
    ).joinToString(",")

    companion object {
        const val SECONDS = 600L
        private fun f(v: Double) = String.format(Locale.US, "%.3f", v).trimEnd('0').trimEnd('.')
        fun parse(line: String): Slice? = try {
            val p = line.split(',')
            Slice(
                start = p[0].toLong(), covered = p[1].toInt(), loadAh = p[2].toDouble(), soc = p[3].toDouble(),
                gas = p[4].toDouble(), tanks = if (p[5].isEmpty()) emptyList() else p[5].split('|').map { it.toDouble() },
                fridgeMin = if (p[6].isEmpty()) Double.NaN else p[6].toDouble(), solarKwh = p[7].toDouble(),
            )
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

    /**
     * A fresh (live) state at wall-clock [nowMs]. Returns a finished slice when this state
     * starts a new one (the caller saves the log then), else null.
     */
    fun add(st: HubState, nowMs: Long): Slice? {
        val dtS = if (lastAtMs > 0) (nowMs - lastAtMs) / 1000.0 else -1.0
        lastAtMs = nowMs
        // A gap over 2 minutes isn't counted: the app didn't see what happened in it.
        val counted = dtS > 0 && dtS <= MAX_GAP_S
        val b = st.bat
        if (b.on) {
            avgA = if (avgA.isNaN() || dtS < 0 || dtS > 300) b.a
            else avgA + (b.a - avgA) * (dtS / AVG_TAU_S).coerceAtMost(1.0)
        } else avgA = Double.NaN

        val key = (nowMs / 1000) / Slice.SECONDS * Slice.SECONDS
        var done: Slice? = null
        var s = current
        if (s == null || s.start != key) {
            if (s != null && s.covered > 0) { push(s); done = s }
            s = Slice(key)
        }
        val fr = st.tmp.getOrNull(2)
        val solarOn = st.sol.filter { it.on }
        current = s.copy(
            covered = s.covered + if (counted) dtS.roundToInt() else 0,
            loadAh = s.loadAh + if (counted && b.on) b.loadA * dtS / 3600.0 else 0.0,
            soc = if (b.on) b.soc else s.soc,
            gas = if (st.gas.on) st.gas.pct else s.gas,
            tanks = st.tank.mapIndexed { i, t -> if (t.on) t.pct else s.tanks.getOrElse(i) { -1.0 } },
            fridgeMin = if (fr != null && fr.on) (if (s.fridgeMin.isNaN()) fr.c else minOf(s.fridgeMin, fr.c)) else s.fridgeMin,
            solarKwh = if (solarOn.isNotEmpty()) solarOn.sumOf { it.yieldKwh } else s.solarKwh,
        )
        return done
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
    fun all(): List<Slice> = slices.toList() + listOfNotNull(current?.takeIf { it.covered > 0 })

    fun outlook(st: HubState, meta: Meta, nowS: Long): Outlook =
        Outlook(battery = batteryText(st.bat, meta.batteryAh, avgA), gas = if (st.gas.on) gasShort(all(), st.gas.pct, nowS) else null)

    companion object {
        const val MAX_GAP_S = 120.0
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
                val whole = halves / 2
                "About $whole${if (halves % 2 == 1) "½" else ""} days"
            }
        }

        fun hm(min: Int) = if (min >= 60) "${min / 60} h ${min % 60} min" else "$min min"

        /**
         * Gas use per day from the bottle % over the last week (since the last refill), as a
         * straight-line fit. null until there's at least 18 hours of readings to go on.
         */
        fun gasPerDay(slices: List<Slice>, nowS: Long): Double? {
            val pts = slices.filter { it.gas >= 0 && it.start >= nowS - 7 * 86400 }
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
            val slope = xs.indices.sumOf { (xs[it] - mx) * (ys[it] - my) } / sxx
            return -slope
        }

        /** Short form for the gas card: "12 days left", "Over 2 weeks", "Barely used", "Measuring". */
        fun gasShort(slices: List<Slice>, pct: Double, nowS: Long): String {
            val perDay = gasPerDay(slices, nowS) ?: return "Measuring"
            if (perDay < 0.5) return "Barely used"
            val t = about((pct / perDay * 1440).roundToInt())
            return if (t.startsWith("Over")) t else t.removePrefix("About ") + " left"
        }

        /** "About 12 days left", "Hardly used lately", "Measuring use…". */
        fun gasText(slices: List<Slice>, pct: Double, nowS: Long): String {
            val perDay = gasPerDay(slices, nowS) ?: return "Measuring use…"
            if (perDay < 0.5) return "Hardly used lately"
            return "${about((pct / perDay * 1440).roundToInt())} left"
        }

        /**
         * The morning summary for the 24 hours up to [nowS]. [dayStart] gives local midnight
         * for a time (so "yesterday" means the phone's yesterday). [live] is the current state if
         * it's live, else null (then nothing "now" is reported). null = the app saw nothing.
         */
        fun summary(
            slices: List<Slice>, nowS: Long, dayStart: (Long) -> Long, meta: Meta, live: HubState?, avgA: Double,
        ): String? {
            val day = slices.filter { it.start >= nowS - 86400 && it.start < nowS }
            val coveredS = day.sumOf { it.covered }
            if (coveredS < 600) return null
            val lines = mutableListOf<String>()

            // Solar: the hub's own "kWh today" counter, highest value seen yesterday.
            val today = dayStart(nowS)
            val yest = slices.filter { it.start >= dayStart(today - 1) && it.start < today && it.solarKwh >= 0 }
            if (yest.isNotEmpty()) lines += "Solar in yesterday: ${fmt1(yest.maxOf { it.solarKwh })} kWh"

            val ah = day.sumOf { it.loadAh }
            val v = live?.bat?.v?.takeIf { it > 0 } ?: 12.8
            lines += "Power used: ${ah.roundToInt()} Ah (about ${fmt1(ah * v / 1000)} kWh)"

            // Water: every drop in each tank (refills ignored).
            val names = meta.tankNames
            val drops = mutableListOf<String>()
            val n = day.maxOfOrNull { it.tanks.size } ?: 0
            for (i in 0 until n) {
                val vals = day.mapNotNull { it.tanks.getOrNull(i)?.takeIf { p -> p >= 0 } }
                var used = 0.0
                for (k in 1 until vals.size) if (vals[k] < vals[k - 1]) used += vals[k - 1] - vals[k]
                if (used >= 1) drops += "${names.getOrElse(i) { "Tank ${i + 1}" }} down ${used.roundToInt()}%"
            }
            if (vals(day).isNotEmpty()) lines += if (drops.isEmpty()) "Water used: none measured" else "Water used: ${drops.joinToString(", ")}"

            // Fridge: lowest overnight (8 pm until now).
            val night = day.filter { it.start >= today - 4 * 3600 && !it.fridgeMin.isNaN() }
            if (night.isNotEmpty()) lines += "Fridge overnight low: ${fmt1(night.minOf { it.fridgeMin })}°C"

            if (live != null && live.bat.on) {
                val left = batteryText(live.bat, meta.batteryAh, avgA)
                lines += "Battery now: ${live.bat.soc.roundToInt()}%" + (left?.let { " · ${it.replaceFirstChar { c -> c.lowercase() }}" } ?: "")
            }
            if (live != null && live.gas.on) {
                lines += "Gas now: ${live.gas.pct.roundToInt()}% · ${gasText(slices, live.gas.pct, nowS).replaceFirstChar { it.lowercase() }}"
            }
            if (coveredS < 86400 * 9 / 10) {
                lines += "(The app heard from the van for ${coveredS / 3600} h ${(coveredS % 3600) / 60} min of the last 24 h, so totals are part of the day.)"
            }
            return lines.joinToString("\n")
        }

        private fun vals(day: List<Slice>) = day.filter { s -> s.tanks.any { it >= 0 } }
        private fun fmt1(v: Double) = String.format(Locale.US, "%.1f", v)
    }
}
