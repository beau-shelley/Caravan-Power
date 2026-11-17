package com.caravan.monitor.ui

// Android glue: wires the shared *Content composables to the hub connection,
// the phone settings and Android intents.

import android.content.Context
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
fun SetupScreen() {
    val app = CaravanApp.instance
    val ctx = LocalContext.current
    val saved by app.prefs.settings.collectAsStateWithLifecycle()
    val status by app.hub.status.collectAsStateWithLifecycle()
    val meta by app.hub.meta.collectAsStateWithLifecycle()
    val update by app.updater.info.collectAsStateWithLifecycle()
    val state by app.hub.state.collectAsStateWithLifecycle()
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
        onScreenSleep = { app.hub.send("display_set", "sleep" to it); app.hub.send("hello") },
        onRemoteControl = { app.hub.send("remote_control", "on" to it); app.hub.send("hello") },
        onSecureCode = { app.hub.send("secure_code", "code" to it); app.hub.send("hello") },
        secSens = state?.sec?.sens ?: 1,
        onSecureSens = { app.hub.send("secure", "sens" to it) },
        onOpenWebPage = { app.hub.baseUrl()?.let { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(it))) } },
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
