package com.caravan.monitor

import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

// What the home-screen widget shows, already worked out and formatted. Pure Kotlin (no
// Android) so it is unit-tested and the screenshot renderer draws the same thing.
//
// No stale data: the widget only shows numbers while the app has live data. Otherwise
// every value is "--" and the bottom line says when the last live data was from.

/** One of the four small tiles: water, gas, inside, fridge. Colours are ARGB. */
data class WidgetTile(val label: String, val value: String = "--", val color: Long = WidgetColors.TEXT)

data class WidgetView(
    val live: Boolean = false,
    /** Bottom line, e.g. "Live · 2:34 pm" or "No connection · last data 2:34 pm". */
    val status: String = "Open the app to connect",
    /** Short form for the small widget: "Live" / "No connection". */
    val statusShort: String = "Not connected",
    /** "2 alerts" while something is unacknowledged, else "". */
    val alerts: String = "",
    val alertColor: Long = WidgetColors.WARN,
    val soc: String = "--",
    /** 0-100 for the bar; 0 while not live. */
    val socPct: Int = 0,
    val socColor: Long = WidgetColors.MUTED,
    /** Net battery current: "Net +12.3 A · charging", "Net -4.2 A · using", "Battery offline". */
    val flow: String = "--",
    val flowColor: Long = WidgetColors.MUTED,
    /** "About 2 days left at this load", "Full in 1 h 20 min" or "". */
    val time: String = "",
    val water: WidgetTile = WidgetTile("Water"),
    val gas: WidgetTile = WidgetTile("Gas"),
    val inside: WidgetTile = WidgetTile("Inside"),
    val fridge: WidgetTile = WidgetTile("Fridge"),
    /** Wall-clock ms of the data shown (live) or of the last live data (not live); 0 unknown. */
    val dataAt: Long = 0,
) {
    /**
     * The same view, blanked: every value "--" and the time of the last live data.
     * Names (tank and thermometer labels) are kept.
     */
    fun blanked(lastAt: Long, now: Long, why: String = "No connection"): WidgetView = WidgetView(
        live = false,
        status = if (lastAt > 0) "$why · last data ${WidgetModel.clock(lastAt, now)}" else why,
        statusShort = why,
        water = WidgetTile(water.label, color = WidgetColors.MUTED),
        gas = WidgetTile(gas.label, color = WidgetColors.MUTED),
        inside = WidgetTile(inside.label, color = WidgetColors.MUTED),
        fridge = WidgetTile(fridge.label, color = WidgetColors.MUTED),
        dataAt = lastAt,
    )

    /**
     * What to draw at [now] from a view saved at [savedAt]: a live view only counts while
     * it's recent (the app refreshes it at least every [WidgetModel.PUSH_EVERY_MS] while
     * live). If the app stopped without saying so, it is shown blanked.
     */
    fun at(now: Long, savedAt: Long): WidgetView =
        if (live && (now - savedAt > WidgetModel.STALE_MS || now < savedAt - 60_000)) blanked(dataAt, now) else this

    fun toJson(): String = JSONObject().apply {
        put("live", live); put("st", status); put("ss", statusShort); put("al", alerts); put("alc", alertColor)
        put("soc", soc); put("sp", socPct); put("sc", socColor); put("fl", flow); put("fc", flowColor); put("tm", time); put("at", dataAt)
        listOf("w" to water, "g" to gas, "i" to inside, "f" to fridge).forEach { (k, t) ->
            put(k, JSONObject().put("l", t.label).put("v", t.value).put("c", t.color))
        }
    }.toString()

    companion object {
        fun fromJson(s: String?): WidgetView? = try {
            if (s.isNullOrBlank()) null else JSONObject(s).let { o ->
                val d = WidgetView()
                fun tile(k: String, def: WidgetTile) = o.optJSONObject(k)?.let {
                    WidgetTile(it.optString("l", def.label), it.optString("v", "--"), it.optLong("c", WidgetColors.TEXT))
                } ?: def
                WidgetView(
                    live = o.optBoolean("live"), status = o.optString("st", d.status), statusShort = o.optString("ss", d.statusShort),
                    alerts = o.optString("al"), alertColor = o.optLong("alc", WidgetColors.WARN),
                    soc = o.optString("soc", "--"), socPct = o.optInt("sp"), socColor = o.optLong("sc", WidgetColors.MUTED),
                    flow = o.optString("fl", "--"), flowColor = o.optLong("fc", WidgetColors.MUTED), time = o.optString("tm"),
                    water = tile("w", d.water), gas = tile("g", d.gas), inside = tile("i", d.inside), fridge = tile("f", d.fridge),
                    dataAt = o.optLong("at"),
                )
            }
        } catch (_: Exception) { null }
    }
}

/** The caravan theme's colours as ARGB (same values as ui/Colors.kt). */
object WidgetColors {
    const val TEXT = 0xFFFFFFFF
    const val MUTED = 0xFF9A9FA5
    const val LIME = 0xFF9BE03A
    const val WATER = 0xFF5CCBFF
    const val GAS = 0xFFFFA35C
    const val WARN = 0xFFFFB020
    const val ALARM = 0xFFFF6B6B
    const val CRIT = 0xFFF07CFF

    fun sev(sev: Int) = when { sev >= Severity.CRIT -> CRIT; sev == Severity.ALARM -> ALARM; else -> WARN }
    fun soc(soc: Double) = when { soc < 20 -> ALARM; soc < 40 -> WARN; else -> LIME }
    fun gas(pct: Double) = when { pct < 10 -> ALARM; pct < 25 -> WARN; else -> GAS }

    /** Same stops as tempColor() in ui/Colors.kt: air (index 0/1) or fridge (index 2). */
    fun temp(index: Int, c: Double): Long {
        val stops = if (index == 2)
            listOf(0.0 to WATER, 2.0 to TEXT, 5.0 to TEXT, 6.5 to WARN, 8.0 to ALARM)
        else
            listOf(10.0 to WATER, 17.0 to TEXT, 23.0 to TEXT, 27.0 to WARN, 30.0 to ALARM)
        if (c <= stops.first().first) return stops.first().second
        for (k in 1 until stops.size) {
            val (t1, c1) = stops[k]
            if (c > t1) continue
            val (t0, c0) = stops[k - 1]
            return lerp(c0, c1, (c - t0) / (t1 - t0))
        }
        return stops.last().second
    }

    private fun lerp(a: Long, b: Long, f: Double): Long {
        var out = 0xFF000000
        for (sh in intArrayOf(16, 8, 0)) {
            val x = (a shr sh) and 0xFF
            val y = (b shr sh) and 0xFF
            out = out or ((x + (y - x) * f).toLong().coerceIn(0, 255) shl sh)
        }
        return out
    }
}

object WidgetModel {
    /** While live, the widget is refreshed at most this often (the app gets a state every second). */
    const val PUSH_EVERY_MS = 30_000L
    /** A saved live view older than this is drawn blanked (the app was stopped). */
    const val STALE_MS = 90_000L

    fun build(st: HubState?, meta: Meta, age: DataAge, outlook: Outlook, conn: ConnStatus, now: Long, previous: WidgetView? = null): WidgetView {
        val names = WidgetView(
            water = WidgetTile(waterName(st, meta)),
            inside = WidgetTile(meta.tempNames.getOrNull(0) ?: "Inside"),
            fridge = WidgetTile(meta.tempNames.getOrNull(2) ?: "Fridge"),
        )
        if (st == null || !age.live) {
            val last = if (age.lastAt > 0) age.lastAt else previous?.dataAt ?: 0L
            val why = if (conn.conn == Conn.IDLE && last == 0L) "Open the app to connect" else "No connection"
            return names.blanked(last, now, why)
        }
        val b = st.bat
        val unacked = st.alerts.filter { !it.ack }
        val t = outlook.battery?.takeIf { it != "Idle" && it != "Charging" } ?: ""
        val tank = st.tank.indexOfFirst { it.on }
        val inside = st.tmp.getOrNull(0)?.takeIf { it.on }
        val fridge = st.tmp.getOrNull(2)?.takeIf { it.on }
        return WidgetView(
            live = true,
            status = (if (conn.cloud) "Live via cloud · " else "Live · ") + clock(now, now),
            statusShort = "Live",
            alerts = when (unacked.size) { 0 -> ""; 1 -> "1 alert"; else -> "${unacked.size} alerts" },
            alertColor = if (unacked.isEmpty()) WidgetColors.WARN else WidgetColors.sev(unacked.maxOf { it.sev }),
            soc = if (b.on) "${Math.round(b.soc)}%" else "--",
            socPct = if (b.on) Math.round(b.soc).toInt().coerceIn(0, 100) else 0,
            socColor = if (b.on) WidgetColors.soc(b.soc) else WidgetColors.MUTED,
            // Net current: all charging in minus all loads out, as measured at the battery.
            flow = when {
                !b.on -> "Battery offline"
                b.a > 0.3 -> "Net +${one(b.a)} A · charging"
                b.a < -0.3 -> "Net -${one(-b.a)} A · using"
                else -> "Net ${one(b.a)} A · idle"
            },
            flowColor = when { !b.on -> WidgetColors.MUTED; b.a > 0.3 -> WidgetColors.LIME; b.a < -0.3 -> WidgetColors.WARN; else -> WidgetColors.TEXT },
            time = if (b.on) t else "",
            water = if (tank >= 0) WidgetTile(names.water.label, "${Math.round(st.tank[tank].pct)}%", WidgetColors.WATER)
                else WidgetTile(names.water.label, color = WidgetColors.MUTED),
            gas = if (st.gas.on) WidgetTile("Gas", "${Math.round(st.gas.pct)}%", WidgetColors.gas(st.gas.pct))
                else WidgetTile("Gas", color = WidgetColors.MUTED),
            // Whole degrees, except the fridge (1 decimal), as everywhere else.
            inside = inside?.let { WidgetTile(names.inside.label, "${Math.round(it.c)}°", WidgetColors.temp(0, it.c)) }
                ?: WidgetTile(names.inside.label, color = WidgetColors.MUTED),
            fridge = fridge?.let { WidgetTile(names.fridge.label, "${one(it.c)}°", WidgetColors.temp(2, it.c)) }
                ?: WidgetTile(names.fridge.label, color = WidgetColors.MUTED),
            dataAt = now,
        )
    }

    /** Whether a new view should be drawn now: straight away when it goes live / not live or an alert changes, else every 30 s. */
    fun shouldPush(next: WidgetView, last: WidgetView?, lastPushAt: Long, now: Long): Boolean = when {
        last == null -> true
        next.live != last.live || next.alerts != last.alerts || next.alertColor != last.alertColor -> true
        !next.live -> next.status != last.status
        else -> now - lastPushAt >= PUSH_EVERY_MS && next.copy(dataAt = 0) != last.copy(dataAt = 0)
    }

    private fun waterName(st: HubState?, meta: Meta): String {
        val i = st?.tank?.indexOfFirst { it.on } ?: -1
        return meta.tankNames.getOrNull(if (i >= 0) i else 0)?.takeIf { it.isNotBlank() && !it.startsWith("Tank ") } ?: "Water"
    }

    private fun one(v: Double) = String.format(Locale.US, "%.1f", v)

    /** "2:34 pm" today, "Tue 2:34 pm" this week, else "12 Nov". */
    fun clock(at: Long, now: Long): String {
        val c = Calendar.getInstance().apply { timeInMillis = at }
        val n = Calendar.getInstance().apply { timeInMillis = now }
        val sameDay = c.get(Calendar.YEAR) == n.get(Calendar.YEAR) && c.get(Calendar.DAY_OF_YEAR) == n.get(Calendar.DAY_OF_YEAR)
        val fmt = when {
            sameDay -> "h:mm a"
            now - at < 6 * 86_400_000L -> "EEE h:mm a"
            else -> "d MMM"
        }
        return SimpleDateFormat(fmt, Locale.ENGLISH).format(Date(at)).replace("AM", "am").replace("PM", "pm")
    }
}
