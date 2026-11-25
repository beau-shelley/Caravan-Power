// Renders every app screen with demo data to android/screenshots/out/*.png.
// Run: cd android/screenshots && ./gradlew run

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.platform.Font
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import com.caravan.monitor.ActiveAlert
import com.caravan.monitor.Battery
import com.caravan.monitor.Conn
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.DataAge
import com.caravan.monitor.Gas
import com.caravan.monitor.Heater
import com.caravan.monitor.HubState
import com.caravan.monitor.Level
import com.caravan.monitor.LevelData
import com.caravan.monitor.Found
import com.caravan.monitor.Place
import com.caravan.monitor.Meta
import com.caravan.monitor.Net
import com.caravan.monitor.Pump
import com.caravan.monitor.Rule
import com.caravan.monitor.Secure
import com.caravan.monitor.Settings
import com.caravan.monitor.Severity
import com.caravan.monitor.Solar
import com.caravan.monitor.Tank
import com.caravan.monitor.Temp
import com.caravan.monitor.Weather
import com.caravan.monitor.UsageLog
import com.caravan.monitor.Outlook
import com.caravan.monitor.ui.AlertsContent
import com.caravan.monitor.ui.CaravanColors
import com.caravan.monitor.ui.caravanBackground
import com.caravan.monitor.ui.ClimateContent
import com.caravan.monitor.ui.AppScaffold
import com.caravan.monitor.ui.AdminContent
import com.caravan.monitor.ui.AdminFinds
import com.caravan.monitor.ui.LevelContent
import com.caravan.monitor.ui.HomeContent
import com.caravan.monitor.ui.RuleEditorCard
import com.caravan.monitor.ui.SetupContent
import com.caravan.monitor.ui.Surfaces
import com.caravan.monitor.ui.Accent
import org.jetbrains.skia.EncodedImageFormat
import java.io.File

// ---------------------------------------------------------------- demo data

val META = Meta(
    solarNames = listOf("Roof", "Portable"),
    tempNames = listOf("Inside", "Outside", "Fridge"),
    tankNames = listOf("Fresh 1", "Fresh 2", "Grey"),
    pumpNames = listOf("Internal", "External"),
    remoteControl = true,
    fw = "1.4.0",
    screenSleepSec = 300,
    batteryAh = 405,
)

// The hub's built-in default rules (CaravanModel.cpp loadDefaultRules).
val RULES = listOf(
    Rule(1, true, "Battery low", "bat_v", false, 12.4, 0.3, 60, Severity.WARN),
    Rule(2, true, "Battery critical", "bat_v", false, 11.8, 0.6, 30, Severity.ALARM, action = "inverter_off"),
    Rule(3, true, "Battery over-voltage", "bat_v", true, 14.8, 0.3, 10, Severity.ALARM),
    Rule(4, true, "Battery charge low", "bat_soc", false, 30.0, 5.0, 60, Severity.WARN, action = "aircon_off",
        levels = listOf(Level(15.0, Severity.ALARM), Level(5.0, Severity.CRIT))),
    Rule(5, true, "High load", "load_a", true, 180.0, 20.0, 15, Severity.WARN),
    Rule(6, true, "Fridge warm", "temp_fridge", true, 6.0, 1.0, 1200, Severity.ALARM),
    Rule(7, true, "Fridge freezing", "temp_fridge", false, -1.0, 1.0, 1800, Severity.WARN, notify = false),
    Rule(8, true, "Inside hot", "temp_in", true, 35.0, 2.0, 300, Severity.WARN),
    Rule(9, true, "Inside cold", "temp_in", false, 5.0, 1.0, 300, Severity.WARN),
    Rule(10, true, "Tank 1 low", "tank1", false, 15.0, 5.0, 30, Severity.INFO),
    Rule(11, true, "Tank 2 low", "tank2", false, 15.0, 5.0, 30, Severity.INFO),
    Rule(12, true, "Grey tank full", "tank3", true, 85.0, 5.0, 30, Severity.WARN),
    Rule(13, false, "Strong wind forecast", "wx_gust", true, 50.0, 10.0, 0, Severity.WARN),
    Rule(14, true, "Gas low", "gas", false, 20.0, 5.0, 60, Severity.WARN),
)

val CONNECTED = ConnStatus(Conn.CONNECTED, "192.168.1.50", "Connected")

/** Midday on mains power: both chargers in bulk, sun on the roof. */
val CHARGING = HubState(
    ts = 1731700000,
    // Roof 3 x 230 W (690 W) and portable 880 W, both near 78 % of rated at midday.
    bat = Battery(on = true, v = 13.84, a = 192.6, soc = 64.0, ttgMin = 34, solarA = 86.2, chargerA = 0.4, loadA = 12.6,
        acA = 119.0, acOn = true),
    sol = listOf(Solar(true, 531.0, 1.92, 538.0, 0), Solar(true, 674.0, 2.41, 686.0, 0)),
    tmp = listOf(Temp(true, 22.6, 17.2, 24.1, 80), Temp(true, 28.4, 14.9, 29.6, 75), Temp(true, 3.4, 2.8, 4.6, 90)),
    tank = listOf(Tank(true, 80.0), Tank(true, 45.0), Tank(true, 30.0)),
    pump = listOf(Pump(true, false, 42), Pump(false, false, 0)),
    alerts = emptyList(),
    wx = Weather(true, "Bright", 27.0, 14.0, 31.0, 2),
    net = Net(true, true, "192.168.1.50"),
    gas = Gas(true, 63.0),
)

/** Evening off-grid: battery running down, fridge warm alarm and a warning. */
val ALERTING = HubState(
    ts = 1731730000,
    bat = Battery(on = true, v = 12.31, a = -18.6, soc = 4.0, ttgMin = 22, solarA = 0.0, chargerA = 0.0, loadA = 18.6),
    sol = listOf(Solar(true, 0.0, 3.12, 541.0, 0), Solar(true, 0.0, 3.96, 690.0, 0)),
    tmp = listOf(Temp(true, 19.4, 17.2, 26.8, 80), Temp(true, 12.1, 11.5, 29.6, 75), Temp(true, 7.4, 2.8, 7.6, 90)),
    tank = listOf(Tank(true, 22.0), Tank(true, 8.0), Tank(true, 62.0)),
    pump = listOf(Pump(false, false, 0), Pump(false, true, 0)),
    alerts = listOf(
        ActiveAlert(4, Severity.CRIT, false, 1731729500, 4.0, "Battery charge low: 4% (< 5)"),
        ActiveAlert(6, Severity.ALARM, false, 1731729000, 7.4, "Fridge temp 7.4 °C (above 6)"),
        ActiveAlert(11, Severity.INFO, true, 1731720000, 8.0, "Fresh 2 8 % (below 15)"),
    ),
    wx = Weather(true, "Bright", 12.0, 6.0, 15.0, 1),
    net = Net(true, false, "192.168.1.50"),
    gas = Gas(true, 63.0),
)

val SETTINGS = Settings(host = "", user = "admin", pass = "secret", backgroundAlerts = true,
    ntfyServer = "https://ntfy.sh", ntfyTopic = "caravan-alerts-8f3k2", cloudHost = "a1b2c3.ala.asia-southeast1.emqxsl.com", cloudPass = "secret")

// ---------------------------------------------------------------- theme

private fun inter(w: FontWeight, file: String) = Font(File("/usr/share/fonts/opentype/inter/$file"), w)

val FONT: FontFamily = runCatching {
    FontFamily(
        inter(FontWeight.Normal, "Inter-Regular.otf"),
        inter(FontWeight.Medium, "Inter-Medium.otf"),
        inter(FontWeight.SemiBold, "Inter-SemiBold.otf"),
        inter(FontWeight.Bold, "Inter-Bold.otf"),
    )
}.getOrDefault(FontFamily.SansSerif)

private fun Typography.withFont(f: FontFamily): Typography {
    fun TextStyle.f() = copy(fontFamily = f)
    return copy(
        displayLarge = displayLarge.f(), displayMedium = displayMedium.f(), displaySmall = displaySmall.f(),
        headlineLarge = headlineLarge.f(), headlineMedium = headlineMedium.f(), headlineSmall = headlineSmall.f(),
        titleLarge = titleLarge.f(), titleMedium = titleMedium.f(), titleSmall = titleSmall.f(),
        bodyLarge = bodyLarge.f(), bodyMedium = bodyMedium.f(), bodySmall = bodySmall.f(),
        labelLarge = labelLarge.f(), labelMedium = labelMedium.f(), labelSmall = labelSmall.f(),
    )
}

@Composable
fun Theme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = CaravanColors,
        typography = Typography().withFont(FONT),
    ) {
        androidx.compose.material3.ProvideTextStyle(MaterialTheme.typography.bodyLarge) { content() }
    }
}

// ---------------------------------------------------------------- rendering

const val WIDTH_DP = 412   // a typical Android phone (e.g. Pixel / Galaxy S) in dp
const val SCALE = 2.5f

fun shot(name: String, heightDp: Int, widthDp: Int = WIDTH_DP, content: @Composable () -> Unit) {
    val scene = ImageComposeScene((widthDp * SCALE).toInt(), (heightDp * SCALE).toInt(), Density(SCALE)) {
        Theme { Box(Modifier.fillMaxSize().caravanBackground()) { content() } }
    }
    // Two frames so layout-dependent things (lazy lists) settle.
    scene.render(0)
    val img = scene.render(16_000_000)
    File("out").mkdirs()
    File("out/$name.png").writeBytes(img.encodeToData(EncodedImageFormat.PNG)!!.bytes)
    scene.close()
    println("out/$name.png")
}

fun main() {
    val nop: () -> Unit = {}
    shot("1-home-mains-charging", 840) {
        AppScaffold(0, {}, 0) {
            HomeContent(CHARGING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop)
        }
    }
    // Same Home on a small phone and a big one: the layout scales and fills the screen.
    shot("14-home-small-phone", 640, 360) {
        AppScaffold(0, {}, 0) { HomeContent(CHARGING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop) }
    }
    shot("15-home-tall-phone", 1000, 412) {
        AppScaffold(0, {}, 0) { HomeContent(CHARGING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop) }
    }
    shot("16-home-big-phone", 1000, 480) {
        AppScaffold(0, {}, 0) { HomeContent(CHARGING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop) }
    }
    shot("2-home-alerts", 840) {
        AppScaffold(0, {}, 2) {
            HomeContent(ALERTING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop)
        }
    }
    shot("3-alerts-and-rules", 2280) {
        AppScaffold(1, {}, 2) {
            AlertsContent(ALERTING, RULES, META, CONNECTED, nop, { _, _ -> }, {}, nop)
        }
    }
    shot("4-edit-rule", 1500) {
        AppScaffold(1, {}, 2) {
            Box(Modifier.fillMaxSize()) {
                AlertsContent(ALERTING, RULES, META, CONNECTED, nop, { _, _ -> }, {}, nop)
                Box(Modifier.fillMaxSize().background(Color(0xAA000000)))
                RuleEditorCard(RULES[3], META, nop, {}, nop,
                    modifier = Modifier.fillMaxWidth().padding(12.dp).align(Alignment.TopCenter), scrollable = false)
            }
        }
    }
    shot("5-setup", 1900) {
        AppScaffold(2, {}, 0) {
            SetupContent(SETTINGS, CONNECTED, META, "0.2.0", batteryExempt = false,
                onSaveSettings = { _, _ -> }, onBatteryExempt = nop)
        }
    }
    // Diesel heater: running on the thermostat on a cold evening (Bluetooth status)
    val cold = ALERTING.copy(alerts = emptyList(),
        tmp = listOf(Temp(true, 18.6, 12.4, 21.0, 80), Temp(true, 7.8, 6.1, 19.5, 75), Temp(true, 3.4, 2.8, 4.6, 90)),
        htr = Heater(link = 1, online = true, phase = 2, mode = 0, setC = 22, level = 6, cabinC = 18.6, bodyC = 142.0, supplyV = 12.9))
    shot("17-home-heater-running", 915) {
        AppScaffold(0, {}, 0) { HomeContent(cold, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop) }
    }
    shot("7-climate-heater", 840) {
        AppScaffold(3, {}, 0) { ClimateContent(cold, META, CONNECTED, { _, _ -> }, nop) }
    }
    shot("8-climate-heater-fault", 840) {
        AppScaffold(3, {}, 0) {
            ClimateContent(cold.copy(htr = cold.htr!!.copy(phase = 0, err = 2, bodyC = 16.0)), META, CONNECTED, { _, _ -> }, nop)
        }
    }
    // Secure mode (anti-theft)
    val calm = CHARGING.copy(sec = Secure(sensorOk = true, sens = 1))
    shot("9-home-secure-off", 840) {
        AppScaffold(0, {}, 0) { HomeContent(calm, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop) }
    }
    shot("10-home-secure-armed", 840) {
        AppScaffold(0, {}, 0) {
            HomeContent(calm.copy(sec = calm.sec.copy(mode = Secure.ARMED, tilt = 0.3, tiltLim = 2.0, turn = 2, turnLim = 20, shakeLim = 45)),
                META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop)
        }
    }
    shot("12-home-movement-entry-delay", 840) {
        AppScaffold(0, {}, 1) {
            HomeContent(calm.copy(sec = calm.sec.copy(mode = Secure.ENTRY, cause = 2, armLeft = 272),
                alerts = listOf(ActiveAlert(14, Severity.WARN, false, 0, 12.0, "Theft alarm: movement - disarm within 5 min"))),
                META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop)
        }
    }
    val theft = calm.copy(sec = calm.sec.copy(mode = Secure.ALARM, cause = 2),
        alerts = listOf(ActiveAlert(14, Severity.CRIT, false, 0, 2.0, "Theft alarm: van turned (being towed?)")))
    shot("11-home-theft-alarm", 840) {
        AppScaffold(0, {}, 1) { HomeContent(theft, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop) }
    }
    shot("13-home-away-cloud", 840) {
        AppScaffold(0, {}, 0) {
            HomeContent(calm, META, RULES, ConnStatus(Conn.CONNECTED, "cloud", "Through the cloud", cloud = true), nop, { _, _ -> }, nop, nop)
        }
    }
    // Lost the link (or the van stopped sending): everything greyed, "No connection" on top.
    val threeHoursAgo = System.currentTimeMillis() - 3 * 3600_000L
    shot("18-home-no-connection", 915) {
        AppScaffold(0, {}, 0) {
            HomeContent(CHARGING, META, RULES, ConnStatus(Conn.OFFLINE, "", "Hub not found. Are you on the caravan's Wi-Fi?"),
                nop, { _, _ -> }, nop, nop,
                age = DataAge(live = false, lastAt = threeHoursAgo, why = "Hub not found. Are you on the caravan's Wi-Fi?"))
        }
    }
    shot("19-home-cloud-van-offline", 915) {
        AppScaffold(0, {}, 0) {
            HomeContent(calm, META, RULES, ConnStatus(Conn.CONNECTED, "cloud", "Through the cloud, but the caravan is offline", cloud = true),
                nop, { _, _ -> }, nop, nop,
                age = DataAge(live = false, lastAt = threeHoursAgo, why = "The cloud is reachable, but the caravan isn't sending anything"))
        }
    }
    shot("20-climate-no-connection", 915) {
        AppScaffold(3, {}, 0) {
            ClimateContent(cold, META, ConnStatus(Conn.OFFLINE, "", "Lost connection, retrying"), { _, _ -> }, nop,
                age = DataAge(live = false, lastAt = System.currentTimeMillis() - 20 * 60_000L, why = "Lost connection, retrying"))
        }
    }
    shot("6-home-not-connected", 915) {
        AppScaffold(0, {}, 0) {
            HomeContent(null, META, emptyList(), ConnStatus(Conn.OFFLINE, "", "Hub not found. Are you on the caravan's Wi-Fi?"),
                nop, { _, _ -> }, nop, nop)
        }
    }
    // Admin settings (behind the admin login) and the Level page
    val adminMeta = META.copy(locSrc = 3, locName = "Yellow Rock NSW", batteryAh = 405, screenSleepSec = 300, secCodeSet = true, tankCal = true)
    val adminFinds = AdminFinds(
        places = listOf(Place("Yellow Rock NSW", "2777, Blue Mountains", -33.69, 150.63), Place("Yellow Rock QLD", "4825", -21.5, 140.8)),
        victron = listOf(Found("d4:3a:2c:91:0e:7b", type = "SmartSolar MPPT 100/30", rssi = -64, slot = 0), Found("c8:11:5e:02:aa:13", type = "SmartSolar MPPT 75/15", rssi = -77)),
        bleTemps = listOf(Found("a4:c1:38:12:34:56", name = "TP357 (3456)", type = "ThermoPro", rssi = -58, c = 21.4, role = 0, batt = 90)),
    )
    shot("18-admin", 4200) {
        AppScaffold(2, {}, 0) {
            AdminContent(CHARGING, adminMeta, CONNECTED, adminFinds, { _, _ -> }, nop, nop, nop, {}, {}, nop, nop)
        }
    }
    shot("19-admin-away-blocked", 900) {
        AppScaffold(2, {}, 0) {
            AdminContent(CHARGING, adminMeta.copy(remoteControl = false), ConnStatus(Conn.CONNECTED, "cloud", "Through the cloud", cloud = true),
                AdminFinds(), { _, _ -> }, nop, nop, nop, {}, {}, nop, nop)
        }
    }
    shot("20-level", 1400) {
        AppScaffold(4, {}, 0) {
            LevelContent(CHARGING.copy(lvl = LevelData(online = true, zeroed = true, frontLearned = true, pitch = 1.2, roll = -0.8)),
                CONNECTED, nop, nop, { _, _ -> }, nop)
        }
    }
    shot("21-level-done", 1400) {
        AppScaffold(4, {}, 0) {
            LevelContent(CHARGING.copy(lvl = LevelData(online = true, zeroed = true, frontLearned = true, pitch = 0.1, roll = -0.2)),
                CONNECTED, nop, nop, { _, _ -> }, nop)
        }
    }
    // Battery time and gas days, worked out on the phone from a demo day.
    val demo = demoUsage()
    shot("22-home-evening-outlook", 840) {
        AppScaffold(0, {}, 0) {
            HomeContent(demo.evening, adminMeta, RULES, CONNECTED, nop, { _, _ -> }, nop, nop, outlook = demo.outlook)
        }
    }
}

class DemoUsage(val evening: HubState, val outlook: Outlook)

/** A day and a bit of made-up readings fed through the real UsageLog, one a minute. */
fun demoUsage(): DemoUsage {
    val tz = 11 * 3600L  // Sydney, summer time
    val dayStart = { t: Long -> Math.floorDiv(t + tz, 86400L) * 86400L - tz }
    val end = dayStart(1764000000L) + 86400L + 7 * 3600L + 60  // 7:01 am local
    val start = end - 34 * 3600L
    val log = UsageLog()
    var soc = 92.0; var gas = 64.0; var fresh = 78.0
    var last: HubState = ALERTING
    var evening: Pair<HubState, Outlook>? = null
    var t = start
    while (t <= end) {
        val h = ((t + tz) % 86400) / 3600.0
        val sun = if (h in 6.5..18.5) Math.sin((h - 6.5) / 12 * Math.PI) else 0.0
        val solarA = 95.0 * sun
        val load = 3.2 + (if ((t / 60) % 30 < 12) 4.0 else 0.0) + (if (h in 18.0..21.0) 6.0 else 0.0)
        val net = solarA - load
        soc = (soc + net / 60 / 405 * 100).coerceIn(0.0, 100.0)
        gas -= 6.0 / 1440 * (if (h in 6.0..9.0 || h in 17.0..20.0) 3.0 else 0.4)
        if (h in 7.0..7.3 || h in 19.0..19.2) fresh -= 0.5
        val ytRoof = 2.9 * (1 - Math.cos(Math.PI * ((h - 6.5) / 12).coerceIn(0.0, 1.0))) / 2
        val fridge = 3.4 + 0.9 * Math.sin(t / 900.0) - (if (h < 7) 0.4 else 0.0)
        last = ALERTING.copy(
            ts = t, alerts = emptyList(),
            bat = Battery(true, 13.2, net, soc, -1, solarA, 0.0, load),
            sol = listOf(Solar(true, solarA * 13.6 * 0.45, ytRoof * 0.45, 540.0, 0), Solar(true, solarA * 13.6 * 0.55, ytRoof * 0.55, 690.0, 0)),
            tmp = listOf(Temp(true, 19.0, 14.0, 24.0, 80), Temp(true, 13.0, 9.0, 27.0, 75), Temp(true, fridge, 2.1, 4.6, 90)),
            tank = listOf(Tank(true, Math.round(fresh).toDouble()), Tank(true, 40.0), Tank(true, 35.0)),
            gas = Gas(true, Math.round(gas * 10) / 10.0),
        )
        log.add(last, t * 1000)
        val meta = META.copy(batteryAh = 405)
        if (evening == null && t - start > 20 * 3600 && ((t + tz) % 86400) / 3600 == 21L) evening = last to log.outlook(last, meta, t)
        t += 60
    }
    val (st, ol) = evening!!
    return DemoUsage(st, ol)
}

