package com.caravan.monitor

import org.json.JSONArray
import org.json.JSONObject

// Kotlin mirror of the hub's JSON (docs/PROTOCOL.md). Parsing is lenient:
// anything missing falls back to "offline" so a partial message never crashes.

data class Battery(
    val on: Boolean, val v: Double, val a: Double, val soc: Double, val ttgMin: Int,
    val solarA: Double, val chargerA: Double, val loadA: Double,
    // Mains chargers (2 x 60 A): no sensor, the hub infers them from what the
    // BMPRO and Victrons don't explain. acOn = bulk mains charging detected.
    val acA: Double = 0.0, val acOn: Boolean = false,
)

data class Solar(val on: Boolean, val w: Double, val yieldKwh: Double, val peakW: Double, val err: Int)
data class Temp(val on: Boolean, val c: Double, val lo: Double, val hi: Double, val batt: Int)
data class Tank(val on: Boolean, val pct: Double)
data class Pump(val on: Boolean, val lockout: Boolean, val runSec: Int)
data class ActiveAlert(val id: Int, val sev: Int, val ack: Boolean, val since: Long, val value: Double, val msg: String)
data class Weather(val ok: Boolean, val place: String, val tempC: Double, val windKmh: Double, val gustKmh: Double, val code: Int)
data class Net(val wifi: Boolean, val cloud: Boolean, val ip: String)

data class HubState(
    val ts: Long,
    val bat: Battery,
    val sol: List<Solar>,
    val tmp: List<Temp>,
    val tank: List<Tank>,
    val pump: List<Pump>,
    val alerts: List<ActiveAlert>,
    val wx: Weather,
    val net: Net,
) {
    companion object {
        fun parse(o: JSONObject): HubState {
            val b = o.optJSONObject("bat") ?: JSONObject()
            val w = o.optJSONObject("wx") ?: JSONObject()
            val n = o.optJSONObject("net") ?: JSONObject()
            return HubState(
                ts = o.optLong("ts"),
                bat = Battery(
                    on = b.optBoolean("on"), v = b.optDouble("v", 0.0), a = b.optDouble("a", 0.0),
                    soc = b.optDouble("soc", 0.0), ttgMin = b.optInt("ttg", -1),
                    solarA = b.optDouble("sa", 0.0), chargerA = b.optDouble("ca", 0.0), loadA = b.optDouble("load", 0.0),
                    acA = b.optDouble("aca", 0.0), acOn = b.optBoolean("acon"),
                ),
                sol = o.optJSONArray("sol").objects().map {
                    Solar(it.optBoolean("on"), it.optDouble("w", 0.0), it.optDouble("yt", 0.0), it.optDouble("mx", 0.0), it.optInt("err"))
                },
                tmp = o.optJSONArray("tmp").objects().map {
                    Temp(it.optBoolean("on"), it.optDouble("c", 0.0), it.optDouble("lo", 0.0), it.optDouble("hi", 0.0), it.optInt("b", -1))
                },
                tank = o.optJSONArray("tank").objects().map { Tank(it.optBoolean("on"), it.optDouble("p", 0.0)) },
                pump = o.optJSONArray("pump").objects().map { Pump(it.optBoolean("on"), it.optBoolean("lk"), it.optInt("rt")) },
                alerts = o.optJSONArray("alerts").objects().map {
                    ActiveAlert(it.optInt("id"), it.optInt("sev", 1), it.optBoolean("ack"), it.optLong("since"),
                        it.optDouble("val", 0.0), it.optString("msg"))
                },
                wx = Weather(w.optBoolean("ok"), w.optString("pl"), w.optDouble("t", 0.0), w.optDouble("w", 0.0),
                    w.optDouble("g", 0.0), w.optInt("c", -1)),
                net = Net(n.optBoolean("wifi"), n.optBoolean("cloud"), n.optString("ip")),
            )
        }
    }
}

data class Meta(
    val solarNames: List<String> = listOf("Solar 1", "Solar 2"),
    val tempNames: List<String> = listOf("Inside", "Outside", "Fridge"),
    val tankNames: List<String> = listOf("Tank 1", "Tank 2", "Tank 3"),
    val pumpNames: List<String> = listOf("Pump 1", "Pump 2"),
    val remoteControl: Boolean = true,
    val fw: String = "",
    /** Touchscreen dim timeout in seconds (0 = never), or -1 if the hub firmware doesn't support it yet. */
    val screenSleepSec: Int = -1,
) {
    companion object {
        fun parse(o: JSONObject): Meta {
            val d = Meta()
            return Meta(
                solarNames = o.optJSONArray("sol").strings().ifEmpty { d.solarNames },
                tempNames = o.optJSONArray("tmp").strings().ifEmpty { d.tempNames },
                tankNames = o.optJSONArray("tank").strings().ifEmpty { d.tankNames },
                pumpNames = o.optJSONArray("pump").strings().ifEmpty { d.pumpNames },
                remoteControl = o.optBoolean("rc", true),
                fw = o.optString("fw"),
                screenSleepSec = o.optInt("ss", -1),
            )
        }
    }
}

/** Same keys, labels, units and +/- steps as kMetrics in CaravanModel.cpp. */
data class Metric(val key: String, val label: String, val unit: String, val step: Double)

object Metrics {
    val all = listOf(
        Metric("bat_v", "Battery voltage", "V", 0.1),
        Metric("bat_soc", "Battery charge", "%", 1.0),
        Metric("bat_a", "Battery current", "A", 1.0),
        Metric("load_a", "Load current", "A", 1.0),
        Metric("charge_a", "Charge current", "A", 1.0),
        Metric("solar_w", "Solar total", "W", 10.0),
        Metric("solar1_w", "Solar 1", "W", 10.0),
        Metric("solar2_w", "Solar 2", "W", 10.0),
        Metric("temp_in", "Inside temp", "°C", 0.5),
        Metric("temp_out", "Outside temp", "°C", 0.5),
        Metric("temp_fridge", "Fridge temp", "°C", 0.5),
        Metric("tank1", "Tank 1", "%", 5.0),
        Metric("tank2", "Tank 2", "%", 5.0),
        Metric("tank3", "Tank 3", "%", 5.0),
        Metric("wx_gust", "Forecast gusts", "km/h", 5.0),
        Metric("wx_rain", "Forecast rain chance", "%", 10.0),
    )

    fun byKey(key: String): Metric = all.firstOrNull { it.key == key } ?: all[0]

    /** Label using the names set on the hub (e.g. "Fresh 1" instead of "Tank 1"). */
    fun label(key: String, meta: Meta): String = when (key) {
        "solar1_w" -> meta.solarNames.getOrNull(0) ?: byKey(key).label
        "solar2_w" -> meta.solarNames.getOrNull(1) ?: byKey(key).label
        "temp_in" -> "${meta.tempNames.getOrNull(0) ?: "Inside"} temp"
        "temp_out" -> "${meta.tempNames.getOrNull(1) ?: "Outside"} temp"
        "temp_fridge" -> "${meta.tempNames.getOrNull(2) ?: "Fridge"} temp"
        "tank1" -> meta.tankNames.getOrNull(0) ?: byKey(key).label
        "tank2" -> meta.tankNames.getOrNull(1) ?: byKey(key).label
        "tank3" -> meta.tankNames.getOrNull(2) ?: byKey(key).label
        else -> byKey(key).label
    }
}

object Actions {
    // inverter_off / aircon_off exist on the hub but those features are hidden for now.
    val visible = listOf("none" to "None", "pumps_off" to "Turn pumps off")
    val all = visible + listOf("inverter_off" to "Turn inverter off", "aircon_off" to "Turn aircon off")
    fun label(key: String) = all.firstOrNull { it.first == key }?.second ?: key
}

object Severity {
    const val INFO = 0
    const val WARN = 1
    const val ALARM = 2
    fun label(sev: Int) = when (sev) { INFO -> "Info"; ALARM -> "Alarm"; else -> "Warning" }
}

data class Rule(
    val id: Int = 0,
    val enabled: Boolean = true,
    val name: String = "",
    val metric: String = "bat_v",
    val above: Boolean = false,
    val threshold: Double = 0.0,
    val hysteresis: Double = 0.0,
    val delaySec: Int = 0,
    val sev: Int = Severity.WARN,
    val notify: Boolean = true,
    val action: String = "none",
) {
    fun toJson(): JSONObject = JSONObject()
        .put("id", id).put("en", enabled).put("name", name).put("m", metric)
        .put("cmp", if (above) ">" else "<").put("thr", threshold).put("hys", hysteresis)
        .put("dly", delaySec).put("sev", sev).put("ntf", notify).put("act", action)

    fun describe(meta: Meta): String {
        val m = Metrics.byKey(metric)
        val dly = if (delaySec > 0) " for ${formatDelay(delaySec)}" else ""
        return "${Metrics.label(metric, meta)} ${if (above) "above" else "below"} ${fmtNum(threshold)} ${m.unit}$dly"
    }

    companion object {
        fun parse(o: JSONObject) = Rule(
            id = o.optInt("id"), enabled = o.optBoolean("en", true), name = o.optString("name"),
            metric = o.optString("m", "bat_v"), above = o.optString("cmp", "<").startsWith(">"),
            threshold = o.optDouble("thr", 0.0), hysteresis = o.optDouble("hys", 0.0),
            delaySec = o.optInt("dly"), sev = o.optInt("sev", Severity.WARN),
            notify = o.optBoolean("ntf", true), action = o.optString("act", "none"),
        )

        fun parseList(a: JSONArray?): List<Rule> = a.objects().map { parse(it) }
    }
}

fun formatDelay(sec: Int): String = when {
    sec <= 0 -> "no delay"
    sec % 3600 == 0 -> "${sec / 3600} h"
    sec % 60 == 0 -> "${sec / 60} min"
    else -> "$sec s"
}

fun fmtNum(v: Double): String =
    if (v == Math.floor(v) && kotlin.math.abs(v) < 1e7) v.toLong().toString()
    else String.format(java.util.Locale.US, "%.2f", v).trimEnd('0').trimEnd('.')

fun JSONArray?.objects(): List<JSONObject> =
    if (this == null) emptyList() else (0 until length()).mapNotNull { optJSONObject(it) }

fun JSONArray?.strings(): List<String> =
    if (this == null) emptyList() else (0 until length()).map { optString(it) }
