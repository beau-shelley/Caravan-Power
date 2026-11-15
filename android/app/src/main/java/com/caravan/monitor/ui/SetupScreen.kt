package com.caravan.monitor.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.PowerManager
import android.provider.Settings as AndroidSettings
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.caravan.monitor.BuildConfig
import com.caravan.monitor.CaravanApp
import com.caravan.monitor.HubClient
import com.caravan.monitor.MonitorService

private val SLEEP_OPTIONS = listOf(0, 1, 2, 5, 10, 15, 30, 60, 120, 240)

@Composable
fun SetupScreen() {
    val app = CaravanApp.instance
    val ctx = LocalContext.current
    val saved by app.prefs.settings.collectAsStateWithLifecycle()
    val status by app.hub.status.collectAsStateWithLifecycle()
    val meta by app.hub.meta.collectAsStateWithLifecycle()
    val connected = status.conn == HubClient.Conn.CONNECTED

    var host by remember(saved) { mutableStateOf(saved.host) }
    var user by remember(saved) { mutableStateOf(saved.user) }
    var pass by remember(saved) { mutableStateOf(saved.pass) }
    var ntfyServer by remember(saved) { mutableStateOf(saved.ntfyServer) }
    var ntfyTopic by remember(saved) { mutableStateOf(saved.ntfyTopic) }
    var ntfyToken by remember(saved) { mutableStateOf(saved.ntfyToken) }

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Setup", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)

        SectionCard("Hub connection", Accent.water) {
            Text(
                when (status.conn) {
                    HubClient.Conn.CONNECTED -> "Connected to ${status.host}"
                    else -> status.detail.ifBlank { "Not connected" }
                },
                color = if (connected) Accent.battery else Accent.warn,
            )
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(host, { host = it }, label = { Text("Hub address") }, singleLine = true,
                placeholder = { Text("Leave blank to find it automatically") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri), modifier = Modifier.fillMaxWidth())
            OutlinedTextField(user, { user = it }, label = { Text("Username") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(pass, { pass = it }, label = { Text("Password") }, singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
            Text("The same login as the hub's web page (WEB_USER / WEB_PASS in secrets.h).",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            Spacer(Modifier.height(8.dp))
            Button(onClick = {
                app.prefs.update(saved.copy(host = host, user = user, pass = pass))
                app.hub.reconnect()
            }) { Text("Save and connect") }
        }

        SectionCard("Alerts on this phone", Accent.warn) {
            SwitchRow("Watch for alerts with the app closed", saved.backgroundAlerts) {
                app.prefs.update(saved.copy(backgroundAlerts = it))
                MonitorService.sync(ctx)
            }
            Text("On the caravan's Wi-Fi the app gets alerts straight from the hub, no internet needed. " +
                "Android shows a small \"Watching caravan\" notification while this is on.",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            if (!ignoringBatteryOptimisation(ctx)) {
                Spacer(Modifier.height(8.dp))
                FilledTonalButton(onClick = { askBatteryExemption(ctx) }) { Text("Stop Android pausing alerts") }
                Text("Some phones pause background apps to save battery, which can delay alerts.",
                    style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            }
            Spacer(Modifier.height(12.dp))
            Text("Away from the van (ntfy)", fontWeight = FontWeight.SemiBold)
            Text("When the van has internet, the hub also sends alerts to an ntfy topic. Enter the same topic as " +
                "NTFY_TOPIC in secrets.h and this app will pick them up anywhere. You don't need the ntfy app.",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            OutlinedTextField(ntfyTopic, { ntfyTopic = it }, label = { Text("ntfy topic") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(ntfyServer, { ntfyServer = it }, label = { Text("ntfy server") }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri), modifier = Modifier.fillMaxWidth())
            OutlinedTextField(ntfyToken, { ntfyToken = it }, label = { Text("Access token (optional)") }, singleLine = true,
                visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            Button(onClick = {
                app.prefs.update(saved.copy(ntfyServer = ntfyServer.ifBlank { "https://ntfy.sh" }, ntfyTopic = ntfyTopic, ntfyToken = ntfyToken))
            }) { Text("Save") }
        }

        SectionCard("Touchscreen", Accent.climate) {
            if (!connected) {
                Text("Connect to the hub to change these.", color = Accent.muted)
            } else {
                if (meta.screenSleepMin >= 0) {
                    Text("Screen sleeps after", style = MaterialTheme.typography.labelMedium, color = Accent.muted)
                    Picker(sleepLabel(meta.screenSleepMin), (SLEEP_OPTIONS + meta.screenSleepMin).distinct().sorted().map { it to sleepLabel(it) }) {
                        app.hub.send("display_set", "sleep" to it)
                        app.hub.send("hello")
                    }
                    Spacer(Modifier.height(8.dp))
                }
                SwitchRow("Allow control from the internet", meta.remoteControl) {
                    app.hub.send("remote_control", "on" to it)
                    app.hub.send("hello")
                }
                Text("When off, the remote dashboard can look but not switch pumps or change settings.",
                    style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            }
        }

        SectionCard("More setup", Accent.muted) {
            Text("Victron keys, thermometers, tank calibration, BMPRO and weather location are coming to the app next. " +
                "Until then, set them on the hub's web page.", style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            Spacer(Modifier.height(8.dp))
            FilledTonalButton(enabled = connected, onClick = {
                app.hub.baseUrl()?.let { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(it))) }
            }) { Text("Open hub web page") }
        }

        Row {
            Text("App ${BuildConfig.VERSION_NAME}" + if (meta.fw.isNotBlank()) " · hub firmware ${meta.fw}" else "",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
        }
        Spacer(Modifier.height(8.dp))
    }
}

private fun sleepLabel(min: Int) = when {
    min == 0 -> "Never"
    min >= 60 && min % 60 == 0 -> "${min / 60} h"
    else -> "$min min"
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
