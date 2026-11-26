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
    val chargeA: Double = 0.0,  // total charge coming in (solar + BMPRO + mains)
)

// a = charge current into the battery (what the app shows); w = panel watts.
data class Solar(val on: Boolean, val w: Double, val yieldKwh: Double, val peakW: Double, val err: Int, val a: Double = 0.0)
data class Temp(val on: Boolean, val c: Double, val lo: Double, val hi: Double, val batt: Int)
data class Tank(val on: Boolean, val pct: Double)
data class Pump(val on: Boolean, val lockout: Boolean, val runSec: Int)
data class Gas(val on: Boolean = false, val pct: Double = 0.0)
/**
 * Diesel heater as the hub reports it ("htr"; absent until one is set up).
 * link 1 = Bluetooth (full status), 2 = 433 MHz remote (no status back).
 * phase 0 off, 1 heating up, 2 running, 3 cooling down. mode 0 thermostat, 1 power level.
 */
data class Heater(
    val link: Int, val online: Boolean, val phase: Int, val mode: Int, val setC: Int, val level: Int,
    val cabinC: Double? = null, val bodyC: Double? = null, val supplyV: Double? = null, val err: Int = 0,
) {
    val running get() = phase == 1 || phase == 2
    val hasStatus get() = link == 1 && online
    companion object {
        const val MIN_C = 8
        const val MAX_C = 36
        const val LEVELS = 10
        val PHASES = listOf("Off", "Heating up", "Running", "Cooling down")
        // Same wording as the touchscreen and web page (CaravanModel.cpp heaterErrorText).
        fun errorText(e: Int) = when (e) {
            1 -> "Didn't start (E-01)"; 2 -> "Out of fuel (E-02)"; 3 -> "Supply voltage fault (E-03)"
            4 -> "Outlet sensor fault (E-04)"; 5 -> "Inlet sensor fault (E-05)"; 6 -> "Fuel pump fault (E-06)"
            7 -> "Fan fault (E-07)"; 8 -> "Glow plug fault (E-08)"; 9 -> "Overheated (E-09)"
            10 -> "Overheat sensor fault (E-10)"; else -> "Heater fault"
        }
        fun parse(h: JSONObject?): Heater? {
            if (h == null || h.optInt("ln") == 0) return null
            return Heater(
                link = h.optInt("ln"), online = h.optBoolean("on"), phase = h.optInt("ph"), mode = h.optInt("md"),
                setC = h.optInt("st", 20), level = h.optInt("lv", 5),
                cabinC = if (h.has("cab")) h.optDouble("cab") else null,
                bodyC = if (h.has("hx")) h.optDouble("hx") else null,
                supplyV = if (h.has("v")) h.optDouble("v") else null,
                err = h.optInt("err"),
            )
        }
    }
}
/**
 * Secure mode (anti-theft), hub key "sec". mode 0 off, 1 arming (exit delay),
 * 2 armed, 3 alarm, 4 movement (5 min entry delay to disarm; armLeft counts it down). cause 1 tilt, 2 turned, 3 shaking, 4 sensor lost.
 * sens 0 low, 1 medium, 2 high. Live movement vs its limits while armed.
 */
data class Secure(
    val mode: Int = 0, val cause: Int = 0, val sens: Int = 1, val sensorOk: Boolean = false,
    val armLeft: Int = 0, val since: Long = 0,
    val tilt: Double = 0.0, val tiltLim: Double = 0.0, val turn: Int = 0, val turnLim: Int = 0,
    val shake: Int = 0, val shakeLim: Int = 0,
) {
    val active get() = mode != OFF
    companion object {
        const val OFF = 0
        const val ARMING = 1
        const val ARMED = 2
        const val ALARM = 3
        const val ENTRY = 4
        val SENS = listOf("Low", "Medium", "High")
        // Same wording as the touchscreen and web page (TheftGuard.cpp secureCauseText).
        fun causeText(c: Int) = when (c) {
            1 -> "van tilted (jacked or hitched?)"; 2 -> "van turned (being towed?)"
            3 -> "van shaking (on the road?)"; 4 -> "movement sensor stopped answering"; else -> "movement"
        }
        fun parse(o: JSONObject?): Secure {
            if (o == null) return Secure()
            return Secure(
                mode = o.optInt("m"), cause = o.optInt("c"), sens = o.optInt("sn", 1), sensorOk = o.optBoolean("ok"),
                armLeft = o.optInt("al"), since = o.optLong("since"),
                tilt = o.optDouble("ti", 0.0), tiltLim = o.optDouble("tl", 0.0), turn = o.optInt("tu"), turnLim = o.optInt("ul"),
                shake = o.optInt("sh"), shakeLim = o.optInt("sl"),
            )
        }
    }
}
/**
 * Caravan level from the hub's tilt sensor ("lvl" in the state), same maths as
 * the touchscreen's Level page (cv::levelAdvice in Leveling.cpp).
 * pitch + = front (hitch) high, roll + = left (door side) high.
 */
data class LevelData(
    val online: Boolean = false,
    val zeroed: Boolean = false,
    val frontLearned: Boolean = false,
    val pitch: Double = 0.0,
    val roll: Double = 0.0,
    val trackMm: Int = 1900,
    val jockeyMm: Int = 4500,
) {
    val rollOk get() = Math.abs(roll) <= TOL
    val pitchOk get() = Math.abs(pitch) <= TOL
    val level get() = zeroed && rollOk && pitchOk
    /** Raise the left wheel (else the right one). */
    val raiseLeft get() = roll < 0
    val wheelMm get() = Math.round(trackMm * Math.abs(Math.tan(Math.toRadians(roll)))).toInt()
    /** + wind the jockey wheel up, - down. */
    val jockeyAdj get() = Math.round(-jockeyMm * Math.tan(Math.toRadians(pitch))).toInt()

    companion object {
        const val TOL = 0.3  // degrees counted as level, same as the touchscreen
        fun parse(o: JSONObject?): LevelData = if (o == null) LevelData() else LevelData(
            online = o.optBoolean("on"), zeroed = o.optBoolean("z"), frontLearned = o.optBoolean("f"),
            pitch = o.optDouble("p", 0.0), roll = o.optDouble("r", 0.0),
            trackMm = o.optInt("tr", 1900), jockeyMm = o.optInt("jk", 4500),
        )
    }
}

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
    val gas: Gas = Gas(),
    val htr: Heater? = null,
    val sec: Secure = Secure(),
    val lvl: LevelData = LevelData(),
) {
    companion object {
        fun parse(o: JSONObject): HubState {
            val b = o.optJSONObject("bat") ?: JSONObject()
            val w = o.optJSONObject("wx") ?: JSONObject()
            val n = o.optJSONObject("net") ?: JSONObject()
            val g = o.optJSONObject("gas") ?: JSONObject()
            return HubState(
                ts = o.optLong("ts"),
                bat = Battery(
                    on = b.optBoolean("on"), v = b.optDouble("v", 0.0), a = b.optDouble("a", 0.0),
                    soc = b.optDouble("soc", 0.0), ttgMin = b.optInt("ttg", -1),
                    solarA = b.optDouble("sa", 0.0), chargerA = b.optDouble("ca", 0.0), loadA = b.optDouble("load", 0.0),
                    acA = b.optDouble("aca", 0.0), acOn = b.optBoolean("acon"),
                    chargeA = b.optDouble("chg", 0.0),
                ),
                sol = o.optJSONArray("sol").objects().map {
                    Solar(it.optBoolean("on"), it.optDouble("w", 0.0), it.optDouble("yt", 0.0), it.optDouble("mx", 0.0), it.optInt("err"), it.optDouble("ba", 0.0))
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
                gas = Gas(g.optBoolean("on"), g.optDouble("p", 0.0)),
                htr = Heater.parse(o.optJSONObject("htr")),
                sec = Secure.parse(o.optJSONObject("sec")),
                lvl = LevelData.parse(o.optJSONObject("lvl")),
            )
        }
    }
}

data class Meta(
    val solarNames: List<String> = listOf("Solar 1", "Solar 2"),
    val tempNames: List<String> = listOf("Inside", "Outside", "Fridge"),
    val tankNames: List<String> = listOf("Tank 1", "Tank 2", "Tank 3"),
    val pumpNames: List<String> = listOf("Internal", "External"),
    val remoteControl: Boolean = true,
    val fw: String = "",
    /** Touchscreen dim timeout in seconds (0 = never), or -1 if the hub firmware doesn't support it yet. */
    val screenSleepSec: Int = -1,
    /** A disarm code is set for the touchscreen (the phone never needs it). */
    val secCodeSet: Boolean = false,
    /** Battery bank size in Ah; the hub works out % and time left from it. 0 = not known yet. */
    val batteryAh: Int = 0,
    /** Weather location: how it was set (0 none, 1 GPS, 2 phone, 3 town search) and its name. */
    val locSrc: Int = 0,
    val locName: String = "",
    /** Tank senders can be calibrated on the hub (false when the levels come from the BMPRO). */
    val tankCal: Boolean = false,
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
                secCodeSet = o.optBoolean("sc"),
                batteryAh = Math.round(o.optDouble("cap", 0.0)).toInt(),
                locSrc = o.optInt("ls"),
                locName = o.optString("ln"),
                tankCal = o.optBoolean("tcal"),
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
        Metric("solar_a", "Solar total", "A", 1.0),
        Metric("solar1_a", "Solar 1", "A", 1.0),
        Metric("solar2_a", "Solar 2", "A", 1.0),
        Metric("temp_in", "Inside temp", "°C", 0.5),
        Metric("temp_out", "Outside temp", "°C", 0.5),
        Metric("temp_fridge", "Fridge temp", "°C", 0.5),
        Metric("tank1", "Tank 1", "%", 5.0),
        Metric("tank2", "Tank 2", "%", 5.0),
        Metric("tank3", "Tank 3", "%", 5.0),
        Metric("wx_gust", "Forecast gusts", "km/h", 5.0),
        Metric("wx_rain", "Forecast rain chance", "%", 10.0),
        Metric("gas", "Gas bottle", "%", 5.0),
        Metric("theft", "Theft alarm", "", 1.0),
    )

    /** Solar rules from a hub that hasn't been updated yet (watts). Shown, not offered. */
    private val legacy = listOf(
        Metric("solar_w", "Solar total", "W", 10.0),
        Metric("solar1_w", "Solar 1", "W", 10.0),
        Metric("solar2_w", "Solar 2", "W", 10.0),
    )

    fun byKey(key: String): Metric = all.firstOrNull { it.key == key } ?: legacy.firstOrNull { it.key == key } ?: all[0]

    /** Label using the names set on the hub (e.g. "Fresh 1" instead of "Tank 1"). */
    fun label(key: String, meta: Meta): String = when (key) {
        "solar1_a", "solar1_w" -> meta.solarNames.getOrNull(0) ?: byKey(key).label
        "solar2_a", "solar2_w" -> meta.solarNames.getOrNull(1) ?: byKey(key).label
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
    const val CRIT = 3   // the top step of a stepped rule, e.g. battery at 5 %
    fun label(sev: Int) = when (sev) { INFO -> "Info"; ALARM -> "Alarm"; CRIT -> "Critical"; else -> "Warning" }
}

/** An extra, deeper step of a rule (hub `lv`): fires once when crossed. */
data class Level(val threshold: Double, val sev: Int)

object Levels {
    const val MAX = 2
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
    /** Up to [Levels.MAX] deeper steps after [threshold], each strictly deeper than the last. */
    val levels: List<Level> = emptyList(),
) {
    fun toJson(): JSONObject = JSONObject()
        .put("id", id).put("en", enabled).put("name", name).put("m", metric)
        .put("cmp", if (above) ">" else "<").put("thr", threshold).put("hys", hysteresis)
        .put("dly", delaySec).put("sev", sev).put("ntf", notify).put("act", action)
        .apply {
            if (levels.isNotEmpty()) put("lv", JSONArray().apply {
                levels.forEach { put(JSONObject().put("thr", it.threshold).put("sev", it.sev)) }
            })
        }

    /** Every step, the first one included: (threshold, severity). */
    val steps: List<Level> get() = listOf(Level(threshold, sev)) + levels

    /** Highest severity this rule can reach. */
    val maxSev: Int get() = steps.maxOf { it.sev }

    fun describe(meta: Meta): String {
        val m = Metrics.byKey(metric)
        val dly = if (delaySec > 0) " for ${formatDelay(delaySec)}" else ""
        val thr = steps.joinToString(" → ") { fmtNum(it.threshold) }
        return "${Metrics.label(metric, meta)} ${if (above) "above" else "below"} $thr ${m.unit}$dly"
    }

    /** Is [t] strictly deeper than [prev] in this rule's direction? */
    fun deeper(t: Double, prev: Double) = if (above) t > prev else t < prev

    /** Drops levels that aren't strictly deeper than the step before (as the hub does). */
    fun withValidLevels(): Rule {
        val out = mutableListOf<Level>()
        var prev = threshold
        for (l in levels) {
            if (out.size >= Levels.MAX) break
            if (deeper(l.threshold, prev)) { out += l; prev = l.threshold }
        }
        return copy(levels = out)
    }

    companion object {
        fun parse(o: JSONObject) = Rule(
            id = o.optInt("id"), enabled = o.optBoolean("en", true), name = o.optString("name"),
            metric = o.optString("m", "bat_v"), above = o.optString("cmp", "<").startsWith(">"),
            threshold = o.optDouble("thr", 0.0), hysteresis = o.optDouble("hys", 0.0),
            delaySec = o.optInt("dly"), sev = o.optInt("sev", Severity.WARN),
            notify = o.optBoolean("ntf", true), action = o.optString("act", "none"),
            levels = o.optJSONArray("lv").objects().map {
                Level(it.optDouble("thr", 0.0), it.optInt("sev", Severity.ALARM).coerceIn(0, Severity.CRIT))
            },
        ).withValidLevels()

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

/** In-app updater state (see Updater.kt). Builds are GitHub releases "app-build-<n>". */
enum class UpdState { NO_TOKEN, IDLE, CHECKING, UP_TO_DATE, AVAILABLE, DOWNLOADING, READY, ERROR }
data class UpdateInfo(
    val state: UpdState = UpdState.IDLE,
    val currentBuild: Int = 0,
    val latestBuild: Int = 0,
    val notes: String = "",
    val progress: Float = 0f,
    val message: String = "",
) {
    /** Worth showing on Home: there's something to download or install. */
    val offer get() = state == UpdState.AVAILABLE || state == UpdState.DOWNLOADING || state == UpdState.READY
}

/** How the app is getting on with the hub (shown in the UI). */
enum class Conn { IDLE, SEARCHING, CONNECTING, CONNECTED, AUTH_FAILED, OFFLINE }
/** [cloud]: going through the cloud broker (away from the van) rather than straight to the hub. */
data class ConnStatus(val conn: Conn = Conn.IDLE, val host: String = "", val detail: String = "", val cloud: Boolean = false)

/**
 * Whether the numbers on screen are live. When they aren't (no link, the link is up but
 * no fresh data is arriving, or the cloud only has an old copy) the app greys everything
 * out and says "No connection" instead of showing old values as if they were current.
 * [lastAt]: wall-clock ms the data on screen is from, 0 if unknown. [why]: one line on the cause.
 */
data class DataAge(val live: Boolean = true, val lastAt: Long = 0, val why: String = "") {
    companion object {
        /** How long without a fresh state before it counts as stale: the hub pushes every second
         *  direct, the cloud copy every 10-15 s. */
        const val LOCAL_STALE_MS = 10_000L
        /** Through the cloud: one missed update plus a few seconds, from the longest send interval
         *  seen (15 s hub -> 25 s, 10 s -> 18 s), never under 15 s or over 30 s. 25 s until known. */
        fun cloudLimitMs(gapMs: Long): Long =
            if (gapMs <= 0) 25_000L else (gapMs * 3 / 2 + 3_000L).coerceIn(15_000L, 30_000L)
        /** A retained cloud state is only trusted as live if its own timestamp is this recent. */
        const val RETAINED_MAX_AGE_S = 25L

        /** "just now", "5 min ago", "3 h ago", "2 days ago". */
        fun ago(lastAt: Long, now: Long): String {
            if (lastAt <= 0) return ""
            val s = ((now - lastAt) / 1000).coerceAtLeast(0)
            return when {
                s < 60 -> "just now"
                s < 3600 -> "${s / 60} min ago"
                s < 2 * 86400 -> "${s / 3600} h ago"
                else -> "${s / 86400} days ago"
            }
        }
    }
}

/** The phone-side settings. Everything about the van itself lives on the hub. */
data class Settings(
    val host: String = "",          // blank = find the hub automatically
    val user: String = "admin",
    val pass: String = "",
    val backgroundAlerts: Boolean = true,
    val ntfyServer: String = "https://ntfy.sh",
    val ntfyTopic: String = "",
    val ntfyToken: String = "",
    // Away from the van: the cloud MQTT broker (EMQX Cloud Serverless). Blank host = off.
    val cloudHost: String = "",     // broker address, e.g. abc123.ala.us-east-1.emqxsl.com (or a full wss:// URL)
    val cloudUser: String = "caravan-dash",
    val cloudPass: String = "",
    val cloudDevice: String = "caravan",  // DEVICE_ID in the hub's secrets.h
) {
    val cloudOn get() = cloudHost.isNotBlank() && cloudPass.isNotEmpty()
    val cloudUrl get() = cloudHost.trim().let { h ->
        if (h.startsWith("wss://") || h.startsWith("ws://")) h else {
            val host = h.removePrefix("https://").trimEnd('/')
            // Secure-WebSocket port: EMQX Serverless uses 8084, HiveMQ Cloud 8884.
            if (host.contains(':')) "wss://$host/mqtt" else "wss://$host:${if (host.contains("emqx")) 8084 else 8884}/mqtt"
        }
    }
}

/** A weather location: from the phone's GPS, a town search, or a quick pick. src as the hub's LocationSource. */
data class Place(val name: String, val detail: String, val lat: Double, val lon: Double, val src: Int = 3) {
    /** The hub keeps 23 characters of the name. */
    val hubName get() = name.take(23)

    companion object {
        const val SRC_PHONE = 2
        const val SRC_SEARCH = 3
        val SRC_LABEL = listOf("", "GPS", "this phone", "town search")
        /** Beau's home spot, offered as a one-tap pick. */
        val HOME = Place("Yellow Rock NSW", "2777, Blue Mountains", -33.6955, 150.6288, SRC_SEARCH)
    }
}

/**
 * Something the hub can hear or has found, from victron_seen, ble_sensors,
 * sensors (wired probes) and heater_seen. Fields a list doesn't have stay at
 * their defaults.
 */
data class Found(
    val addr: String,
    val name: String = "",
    val type: String = "",
    val rssi: Int = 0,
    val c: Double? = null,
    val role: Int = -1,
    val slot: Int = -1,
    val batt: Int = -1,
) {
    companion object {
        fun parseList(a: JSONArray?): List<Found> = a.objects().map {
            Found(
                addr = it.optString("addr"), name = it.optString("name"),
                type = it.optString("type").ifBlank { it.optString("model") },
                rssi = it.optInt("rssi"), c = if (it.has("c")) it.optDouble("c") else null,
                role = it.optInt("role", -1), slot = it.optInt("slot", -1), batt = it.optInt("batt", -1),
            )
        }
    }
}
