package com.caravan.monitor.ui

// Android glue: wires the shared *Content composables to the hub connection,
// the phone settings and Android intents.

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.SystemClock
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.core.content.ContextCompat
import com.caravan.monitor.Found
import com.caravan.monitor.Geo
import com.caravan.monitor.Place
import kotlinx.coroutines.launch
import android.content.Intent
import android.net.Uri
import android.os.PowerManager
import android.provider.Settings as AndroidSettings
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.caravan.monitor.BuildConfig
import com.caravan.monitor.CaravanApp
import com.caravan.monitor.MonitorService
import com.caravan.monitor.Rule
import com.caravan.monitor.Secure
import com.caravan.monitor.UpdState
import org.json.JSONObject

@Composable
fun HomeScreen(onOpenAlerts: () -> Unit, onOpenSetup: () -> Unit) {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    val meta by hub.meta.collectAsStateWithLifecycle()
    val rules by hub.rules.collectAsStateWithLifecycle()
    val status by hub.status.collectAsStateWithLifecycle()
    val updater = CaravanApp.instance.updater
    val update by updater.info.collectAsStateWithLifecycle()
    var askSecure by remember { mutableStateOf<Boolean?>(null) }  // true = arm, false = disarm
    HomeContent(
        state, meta, rules, status,
        onAck = { hub.send("ack") },
        onPump = { i, on -> hub.send("pump", "i" to i, "on" to on) },
        onOpenAlerts = onOpenAlerts,
        onOpenSetup = onOpenSetup,
        update = update,
        onUpdate = { updater.startOrInstall() },
        onSecure = { askSecure = it },
        onArm = { hub.send("secure", "arm" to true) },  // uses the sensitivity set in Setup
    )
    // Arming is the slide on the Secure card; only disarming asks first.
    if (askSecure == false) {
        val sec = state?.sec ?: Secure()
        AlertDialog(
            onDismissRequest = { askSecure = null },
            title = { Text(if (sec.mode == Secure.ALARM || sec.mode == Secure.ENTRY) "Turn the theft alarm off?" else "Turn secure mode off?") },
            text = { Text("The hub stops watching for movement.") },
            confirmButton = { TextButton(onClick = { hub.send("secure", "arm" to false); askSecure = null }) { Text("Disarm") } },
            dismissButton = { TextButton(onClick = { askSecure = null }) { Text("Cancel") } },
        )
    }
}

@Composable
fun ClimateScreen(onOpenSetup: () -> Unit) {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    val meta by hub.meta.collectAsStateWithLifecycle()
    val status by hub.status.collectAsStateWithLifecycle()
    ClimateContent(state, meta, status, onHeater = { k, v -> hub.send("heater", k to v) }, onOpenSetup = onOpenSetup)
}

@Composable
fun AlertsScreen() {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    val rules by hub.rules.collectAsStateWithLifecycle()
    val meta by hub.meta.collectAsStateWithLifecycle()
    val status by hub.status.collectAsStateWithLifecycle()
    var editing by remember { mutableStateOf<Rule?>(null) }
    var confirmReset by remember { mutableStateOf(false) }

    AlertsContent(
        state, rules, meta, status,
        onAck = { hub.send("ack") },
        onToggleRule = { r, on -> hub.send(JSONObject().put("c", "rule_set").put("rule", r.copy(enabled = on).toJson())) },
        onEditRule = { editing = it },
        onResetRules = { confirmReset = true },
    )

    editing?.let { r ->
        Dialog(onDismissRequest = { editing = null }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
            RuleEditorCard(
                initial = r,
                meta = meta,
                onDismiss = { editing = null },
                onSave = { hub.send(JSONObject().put("c", "rule_set").put("rule", it.toJson())); editing = null },
                onDelete = if (r.id != 0) ({ hub.send("rule_del", "id" to r.id); editing = null }) else null,
                modifier = Modifier.fillMaxWidth().padding(12.dp).imePadding(),
            )
        }
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Restore default rules?") },
            text = { Text("This replaces all your alert rules with the hub's built-in defaults.") },
            confirmButton = { TextButton(onClick = { hub.send("rules_reset"); confirmReset = false }) { Text("Restore") } },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text("Cancel") } },
        )
    }
}

@Composable
fun SetupScreen(onOpenAdmin: () -> Unit) {
    val app = CaravanApp.instance
    val ctx = LocalContext.current
    val saved by app.prefs.settings.collectAsStateWithLifecycle()
    val status by app.hub.status.collectAsStateWithLifecycle()
    val meta by app.hub.meta.collectAsStateWithLifecycle()
    val update by app.updater.info.collectAsStateWithLifecycle()
    var askLogin by remember { mutableStateOf(false) }
    SetupContent(
        saved = saved,
        status = status,
        meta = meta,
        appVersion = BuildConfig.VERSION_NAME,
        batteryExempt = ignoringBatteryOptimisation(ctx),
        onSaveSettings = { s, reconnect ->
            val bgChanged = s.backgroundAlerts != saved.backgroundAlerts
            app.prefs.update(s)
            if (reconnect) app.hub.reconnect()
            if (bgChanged) MonitorService.sync(ctx)
        },
        onBatteryExempt = { askBatteryExemption(ctx) },
        onOpenAdmin = { if (AdminSession.unlocked()) onOpenAdmin() else askLogin = true },
        adminUnlocked = AdminSession.unlocked(),
        update = update,
        updateTokenSet = update.state != UpdState.NO_TOKEN,
        onSaveUpdateToken = { app.updater.setToken(it) },
        onCheckUpdate = { app.updater.check(force = true) },
        onUpdate = { app.updater.startOrInstall() },
        onOpenTokenPage = {
            ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(
                "https://github.com/settings/personal-access-tokens/new?name=Caravan%20app%20updates" +
                    "&description=Lets%20the%20Caravan%20Android%20app%20download%20its%20updates&contents=read&target_name=beau-shelley&expires_in=none")))
        },
    )
    if (askLogin) AdminLoginDialog(onDismiss = { askLogin = false }, onOk = { askLogin = false; onOpenAdmin() })
}

/**
 * Admin login, held in memory only: it ends when the app is closed, when
 * "Log out" is pressed, or 30 minutes after the last admin change.
 */
object AdminSession {
    private const val IDLE_MS = 30 * 60_000L
    private var until by mutableStateOf(0L)
    fun unlocked() = SystemClock.elapsedRealtime() < until
    fun touch() { until = SystemClock.elapsedRealtime() + IDLE_MS }
    fun logout() { until = 0L }
}

/**
 * The admin password is the hub's own web login (WEB_USER / WEB_PASS in
 * secrets.h). On the caravan's Wi-Fi the hub checks it. Away from the van the
 * hub can't be asked directly, so the password has to match the hub password
 * or the cloud password already saved in this app's Setup.
 */
@Composable
private fun AdminLoginDialog(onDismiss: () -> Unit, onOk: () -> Unit) {
    val app = CaravanApp.instance
    val saved by app.prefs.settings.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    var user by remember { mutableStateOf(saved.user.ifBlank { "admin" }) }
    var pass by remember { mutableStateOf("") }
    var err by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    fun tryLogin() {
        if (busy) return
        busy = true
        err = ""
        scope.launch {
            val direct = app.hub.checkLogin(user, pass)
            val ok = direct ?: (pass.isNotEmpty() && (pass == saved.pass || pass == saved.cloudPass))
            busy = false
            if (ok) {
                AdminSession.touch()
                if (direct == true && (user != saved.user || pass != saved.pass)) app.prefs.update(saved.copy(user = user, pass = pass))
                onOk()
            } else {
                err = if (direct == false) "The hub says that's the wrong username or password."
                else "Wrong password. Away from the van, use the hub password or the cloud password saved in Setup."
            }
        }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Admin login") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("The same login as the hub's web page.")
                OutlinedTextField(user, { user = it.trim() }, label = { Text("Username") }, singleLine = true)
                OutlinedTextField(pass, { pass = it }, label = { Text("Password") }, singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { tryLogin() }))
                if (err.isNotEmpty()) Text(err, color = Accent.warn)
            }
        },
        confirmButton = { TextButton(onClick = { tryLogin() }, enabled = !busy) { Text(if (busy) "Checking..." else "Log in") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

/** Commands whose answer changes the names/settings in "meta": ask for a fresh copy after them. */
private val META_CMDS = setOf("battery_set", "display_set", "remote_control", "secure_code", "location_set", "heater_set", "victron_set", "temp_set", "sensor_assign")

@Composable
fun AdminScreen(snack: SnackbarHostState, onBack: () -> Unit, onOpenAlerts: () -> Unit, onOpenLevel: () -> Unit) {
    val hub = CaravanApp.instance.hub
    val ctx = LocalContext.current
    val state by hub.state.collectAsStateWithLifecycle()
    val meta by hub.meta.collectAsStateWithLifecycle()
    val status by hub.status.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    var finds by remember { mutableStateOf(AdminFinds()) }
    BackHandler(onBack = onBack)

    LaunchedEffect(Unit) {
        hub.replies.collect { r ->
            val list = Found.parseList(r.optJSONArray("list"))
            when (r.optString("t")) {
                "victron_seen" -> finds = finds.copy(victron = list)
                "ble_sensors" -> finds = finds.copy(bleTemps = list)
                "sensors" -> finds = finds.copy(wiredTemps = list)
                "heater_seen" -> finds = finds.copy(heaters = list)
                "heater_log" -> finds = finds.copy(heaterLog = r.optString("log"))
                "bmpro_log" -> finds = finds.copy(bmproLog = r.optString("log"))
                "ok", "meta" -> scope.launch { snack.showSnackbar("Saved") }
                "tank_cal" -> scope.launch { snack.showSnackbar("Stored") }
            }
        }
    }

    fun send(c: String, kv: Map<String, Any>) {
        if (!AdminSession.unlocked()) {
            scope.launch { snack.showSnackbar("Logged out after 30 minutes. Log in again.") }
            onBack()
            return
        }
        AdminSession.touch()
        val o = JSONObject().put("c", c)
        kv.forEach { (k, v) -> o.put(k, v) }
        if (!hub.send(o)) { scope.launch { snack.showSnackbar("Not connected to the hub") }; return }
        if (c in META_CMDS) hub.send("hello")
    }

    fun pick(p: Place) {
        send("location_set", mapOf("lat" to p.lat, "lon" to p.lon, "name" to p.hubName, "src" to p.src))
        finds = finds.copy(places = null, locMsg = "")
    }

    val usePhone = rememberPhoneLocation(
        onBusy = { finds = finds.copy(locBusy = it, locMsg = if (it) "" else finds.locMsg) },
        onPlace = { pick(it) },
        onError = { finds = finds.copy(locMsg = it) },
    )

    AdminContent(
        state, meta, status, finds,
        send = ::send,
        onBack = onBack,
        onLogout = { AdminSession.logout(); onBack() },
        onUsePhoneLocation = usePhone,
        onSearchTown = { q ->
            finds = finds.copy(locBusy = true, locMsg = "")
            scope.launch {
                finds = try {
                    finds.copy(places = Geo.search(q), locBusy = false)
                } catch (e: Exception) {
                    finds.copy(locBusy = false, locMsg = e.message ?: "Search failed")
                }
            }
        },
        onPickPlace = ::pick,
        onOpenAlerts = onOpenAlerts,
        onOpenLevel = onOpenLevel,
        onOpenWebPage = hub.baseUrl()?.let { url -> { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) } },
    )
}

@Composable
fun LevelScreen(snack: SnackbarHostState) {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    val status by hub.status.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) {
        hub.replies.collect { r -> if (r.optString("t") == "level" && r.optString("msg").isNotBlank()) scope.launch { snack.showSnackbar(r.optString("msg")) } }
    }
    val usePhone = rememberPhoneLocation(
        onBusy = {},
        onPlace = { p ->
            hub.send(JSONObject().put("c", "location_set").put("lat", p.lat).put("lon", p.lon).put("name", p.hubName).put("src", p.src))
            hub.send("hello")
            scope.launch { snack.showSnackbar("Weather location set to ${p.name}") }
        },
        onError = { scope.launch { snack.showSnackbar(it) } },
    )
    LevelContent(
        state, status,
        onZero = { hub.send("level_zero") },
        onFront = { hub.send("level_front") },
        onDims = { t, j -> hub.send("level_dims", "track" to t, "jockey" to j) },
        onSetLocationHere = usePhone,
    )
}

/** Returns an action that asks for the location permission if needed, then finds where the phone is. */
@Composable
private fun rememberPhoneLocation(onBusy: (Boolean) -> Unit, onPlace: (Place) -> Unit, onError: (String) -> Unit): () -> Unit {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val run = {
        onBusy(true)
        scope.launch {
            try {
                onPlace(Geo.here(ctx))
            } catch (e: Exception) {
                onError(e.message ?: "Couldn't get the phone's location")
            } finally {
                onBusy(false)
            }
        }
        Unit
    }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { res ->
        if (res.values.any { it }) run() else onError("Location permission was refused. Use the town search instead.")
    }
    return {
        val granted = ContextCompat.checkSelfPermission(ctx, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (granted) run() else ask.launch(arrayOf(Manifest.permission.ACCESS_COARSE_LOCATION, Manifest.permission.ACCESS_FINE_LOCATION))
    }
}

private fun ignoringBatteryOptimisation(ctx: Context): Boolean {
    val pm = ctx.getSystemService(Context.POWER_SERVICE) as PowerManager
    return pm.isIgnoringBatteryOptimizations(ctx.packageName)
}

@android.annotation.SuppressLint("BatteryLife") // sideloaded app whose whole job is background alerts
private fun askBatteryExemption(ctx: Context) {
    try {
        ctx.startActivity(Intent(AndroidSettings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:${ctx.packageName}")))
    } catch (_: Exception) {
        ctx.startActivity(Intent(AndroidSettings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
    }
}
