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
import com.caravan.monitor.Gas
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import com.caravan.monitor.Net
import com.caravan.monitor.Pump
import com.caravan.monitor.Rule
import com.caravan.monitor.Settings
import com.caravan.monitor.Severity
import com.caravan.monitor.Solar
import com.caravan.monitor.Tank
import com.caravan.monitor.Temp
import com.caravan.monitor.Weather
import com.caravan.monitor.ui.AlertsContent
import com.caravan.monitor.ui.AppScaffold
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
)

// The hub's built-in default rules (CaravanModel.cpp loadDefaultRules).
val RULES = listOf(
    Rule(1, true, "Battery low", "bat_v", false, 12.4, 0.3, 60, Severity.WARN),
    Rule(2, true, "Battery critical", "bat_v", false, 11.8, 0.6, 30, Severity.ALARM, action = "inverter_off"),
    Rule(3, true, "Battery over-voltage", "bat_v", true, 14.8, 0.3, 10, Severity.ALARM),
    Rule(4, true, "Charge below 30%", "bat_soc", false, 30.0, 5.0, 60, Severity.WARN),
    Rule(5, true, "Charge below 15%", "bat_soc", false, 15.0, 5.0, 60, Severity.ALARM, action = "aircon_off"),
    Rule(6, true, "High load", "load_a", true, 180.0, 20.0, 15, Severity.WARN),
    Rule(7, true, "Fridge warm", "temp_fridge", true, 6.0, 1.0, 1200, Severity.ALARM),
    Rule(8, true, "Fridge freezing", "temp_fridge", false, -1.0, 1.0, 1800, Severity.WARN, notify = false),
    Rule(9, true, "Inside hot", "temp_in", true, 35.0, 2.0, 300, Severity.WARN),
    Rule(10, true, "Inside cold", "temp_in", false, 5.0, 1.0, 300, Severity.WARN),
    Rule(11, true, "Tank 1 low", "tank1", false, 15.0, 5.0, 30, Severity.INFO),
    Rule(12, true, "Tank 2 low", "tank2", false, 15.0, 5.0, 30, Severity.INFO),
    Rule(13, true, "Grey tank full", "tank3", true, 85.0, 5.0, 30, Severity.WARN),
    Rule(14, false, "Strong wind forecast", "wx_gust", true, 50.0, 10.0, 0, Severity.WARN),
    Rule(15, true, "Gas low", "gas", false, 20.0, 5.0, 60, Severity.WARN),
)

val CONNECTED = ConnStatus(Conn.CONNECTED, "192.168.1.50", "Connected")

/** Midday on mains power: both chargers in bulk, sun on the roof. */
val CHARGING = HubState(
    ts = 1731700000,
    bat = Battery(on = true, v = 13.62, a = 131.4, soc = 64.0, ttgMin = 25, solarA = 21.8, chargerA = 3.1, loadA = 12.6,
        acA = 119.0, acOn = true),
    sol = listOf(Solar(true, 296.0, 1.42, 388.0, 0), Solar(true, 0.0, 0.0, 0.0, 0)),
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
    bat = Battery(on = true, v = 12.31, a = -18.6, soc = 27.0, ttgMin = 248, solarA = 0.0, chargerA = 0.0, loadA = 18.6),
    sol = listOf(Solar(true, 0.0, 2.81, 512.0, 0), Solar(true, 0.0, 0.64, 140.0, 0)),
    tmp = listOf(Temp(true, 19.4, 17.2, 26.8, 80), Temp(true, 12.1, 11.5, 29.6, 75), Temp(true, 7.4, 2.8, 7.6, 90)),
    tank = listOf(Tank(true, 22.0), Tank(true, 8.0), Tank(true, 62.0)),
    pump = listOf(Pump(false, false, 0), Pump(false, true, 0)),
    alerts = listOf(
        ActiveAlert(7, Severity.ALARM, false, 1731729000, 7.4, "Fridge temp 7.4 °C (above 6)"),
        ActiveAlert(4, Severity.WARN, false, 1731729500, 27.0, "Battery charge 27 % (below 30)"),
        ActiveAlert(12, Severity.INFO, true, 1731720000, 8.0, "Fresh 2 8 % (below 15)"),
    ),
    wx = Weather(true, "Bright", 12.0, 6.0, 15.0, 1),
    net = Net(true, false, "192.168.1.50"),
    gas = Gas(true, 63.0),
)

val SETTINGS = Settings(host = "", user = "admin", pass = "secret", backgroundAlerts = true,
    ntfyServer = "https://ntfy.sh", ntfyTopic = "caravan-alerts-8f3k2")

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
        colorScheme = darkColorScheme(
            primary = Color(0xFF4DA3FF), onPrimary = Color(0xFF00213F), secondary = Accent.battery,
            background = Surfaces.background, surface = Surfaces.background, onSurface = Surfaces.text,
            onBackground = Surfaces.text, surfaceVariant = Surfaces.raised, error = Accent.alarm,
            secondaryContainer = Color(0xFF1F3A57), onSecondaryContainer = Surfaces.text,
            primaryContainer = Color(0xFF1F3A57), onPrimaryContainer = Surfaces.text,
        ),
        typography = Typography().withFont(FONT),
    ) {
        androidx.compose.material3.ProvideTextStyle(MaterialTheme.typography.bodyLarge) { content() }
    }
}

// ---------------------------------------------------------------- rendering

const val WIDTH_DP = 412   // a typical Android phone (e.g. Pixel / Galaxy S) in dp
const val SCALE = 2.5f

fun shot(name: String, heightDp: Int, content: @Composable () -> Unit) {
    val scene = ImageComposeScene((WIDTH_DP * SCALE).toInt(), (heightDp * SCALE).toInt(), Density(SCALE)) {
        Theme { Box(Modifier.fillMaxSize().background(Surfaces.background)) { content() } }
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
    shot("1-home-mains-charging", 1310) {
        AppScaffold(0, {}, 0) {
            HomeContent(CHARGING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop)
        }
    }
    shot("2-home-alerts", 1400) {
        AppScaffold(0, {}, 2) {
            HomeContent(ALERTING, META, RULES, CONNECTED, nop, { _, _ -> }, nop, nop)
        }
    }
    shot("3-alerts-and-rules", 2280) {
        AppScaffold(1, {}, 2) {
            AlertsContent(ALERTING, RULES, META, CONNECTED, nop, { _, _ -> }, {}, nop)
        }
    }
    shot("4-edit-rule", 1150) {
        AppScaffold(1, {}, 2) {
            Box(Modifier.fillMaxSize()) {
                AlertsContent(ALERTING, RULES, META, CONNECTED, nop, { _, _ -> }, {}, nop)
                Box(Modifier.fillMaxSize().background(Color(0xAA000000)))
                RuleEditorCard(RULES[6], META, nop, {}, nop,
                    modifier = Modifier.fillMaxWidth().padding(12.dp).align(Alignment.TopCenter), scrollable = false)
            }
        }
    }
    shot("5-setup", 1500) {
        AppScaffold(2, {}, 0) {
            SetupContent(SETTINGS, CONNECTED, META, "0.2.0", batteryExempt = false,
                onSaveSettings = { _, _ -> }, onBatteryExempt = nop, onScreenSleep = {}, onRemoteControl = {}, onOpenWebPage = nop)
        }
    }
    shot("6-home-not-connected", 915) {
        AppScaffold(0, {}, 0) {
            HomeContent(null, META, emptyList(), ConnStatus(Conn.OFFLINE, "", "Hub not found. Are you on the caravan's Wi-Fi?"),
                nop, { _, _ -> }, nop, nop)
        }
    }
}
