package com.caravan.monitor.ui

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
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.caravan.monitor.Conn
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.Meta
import com.caravan.monitor.Secure
import com.caravan.monitor.Settings
import com.caravan.monitor.UpdateInfo

private val SLEEP_OPTIONS = listOf(10, 20, 30, 60, 300, 900, 1800, 3600, 0)

@OptIn(ExperimentalMaterial3Api::class) // FilterChip is still experimental in older Material 3
@Composable
fun SetupContent(
    saved: Settings,
    status: ConnStatus,
    meta: Meta,
    appVersion: String,
    batteryExempt: Boolean,
    onSaveSettings: (Settings, reconnect: Boolean) -> Unit,
    onBatteryExempt: () -> Unit,
    onScreenSleep: (Int) -> Unit,
    onRemoteControl: (Boolean) -> Unit,
    /** Battery bank size in Ah (20-2000). */
    onBatteryAh: (Int) -> Unit = {},
    /** Tell the hub the battery is full now (sets its % to 100). */
    onBatteryFull: () -> Unit = {},
    onOpenWebPage: () -> Unit,
    /** Disarm code for the touchscreen: 4-8 digits, "" removes it. */
    onSecureCode: (String) -> Unit = {},
    /** Secure mode sensitivity (0 low, 1 medium, 2 high), used every time it is armed. */
    secSens: Int = 1,
    onSecureSens: (Int) -> Unit = {},
    update: UpdateInfo = UpdateInfo(),
    updateTokenSet: Boolean = false,
    onSaveUpdateToken: (String) -> Unit = {},
    onCheckUpdate: () -> Unit = {},
    onUpdate: () -> Unit = {},
    onOpenTokenPage: () -> Unit = {},
) {
    val connected = status.conn == Conn.CONNECTED

    var host by remember(saved) { mutableStateOf(saved.host) }
    var user by remember(saved) { mutableStateOf(saved.user) }
    var pass by remember(saved) { mutableStateOf(saved.pass) }
    var ntfyServer by remember(saved) { mutableStateOf(saved.ntfyServer) }
    var ntfyTopic by remember(saved) { mutableStateOf(saved.ntfyTopic) }
    var ntfyToken by remember(saved) { mutableStateOf(saved.ntfyToken) }
    var cloudHost by remember(saved) { mutableStateOf(saved.cloudHost) }
    var cloudUser by remember(saved) { mutableStateOf(saved.cloudUser) }
    var cloudPass by remember(saved) { mutableStateOf(saved.cloudPass) }
    var cloudDevice by remember(saved) { mutableStateOf(saved.cloudDevice) }

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Setup", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)

        SectionCard("Hub connection", Accent.lime) {
            Text(
                when (status.conn) {
                    Conn.CONNECTED -> if (status.cloud) status.detail else "Connected to ${status.host}"
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
            Button(onClick = { onSaveSettings(saved.copy(host = host, user = user, pass = pass), true) }) { Text("Save and connect") }
        }

        SectionCard("Away from the van (cloud)", Accent.solar) {
            Text("When the hub isn't on this Wi-Fi, the app goes through your cloud MQTT broker instead, " +
                "so you can see the van and arm it from anywhere the van has internet. Back on the van's Wi-Fi it " +
                "goes direct again by itself. Use the caravan-dash login, not the hub's.",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            OutlinedTextField(cloudHost, { cloudHost = it }, label = { Text("Broker address") }, singleLine = true,
                placeholder = { Text("xxxx.emqxsl.com") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri), modifier = Modifier.fillMaxWidth())
            OutlinedTextField(cloudUser, { cloudUser = it }, label = { Text("Cloud username") }, singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(cloudPass, { cloudPass = it }, label = { Text("Cloud password") }, singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
            OutlinedTextField(cloudDevice, { cloudDevice = it }, label = { Text("Device id") }, singleLine = true,
                supportingText = { Text("DEVICE_ID in the hub's secrets.h, normally caravan") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = {
                    onSaveSettings(saved.copy(cloudHost = cloudHost, cloudUser = cloudUser, cloudPass = cloudPass,
                        cloudDevice = cloudDevice.ifBlank { "caravan" }), true)
                }) { Text("Save") }
                if (saved.cloudOn) OutlinedButton(onClick = { onSaveSettings(saved.copy(cloudHost = "", cloudPass = ""), true) }) { Text("Turn off") }
            }
        }

        SectionCard("Alerts on this phone", Accent.warn) {
            SwitchRow("Watch for alerts with the app closed", saved.backgroundAlerts) {
                onSaveSettings(saved.copy(backgroundAlerts = it), false)
            }
            Text("On the caravan's Wi-Fi the app gets alerts straight from the hub, no internet needed. " +
                "Android shows a small \"Watching caravan\" notification while this is on.",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            if (!batteryExempt) {
                Spacer(Modifier.height(8.dp))
                FilledTonalButton(onClick = onBatteryExempt) { Text("Stop Android pausing alerts") }
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
                onSaveSettings(saved.copy(ntfyServer = ntfyServer.ifBlank { "https://ntfy.sh" }, ntfyTopic = ntfyTopic, ntfyToken = ntfyToken), false)
            }) { Text("Save") }
        }

        SectionCard("Battery", Accent.battery) {
            Text("The hub works out the battery % and time left itself, by counting the amps in and out " +
                "against this size. (The BMPRO's own % stops at 600 Ah.) It resets to 100% each time the " +
                "charger shows the bank is full.",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            Spacer(Modifier.height(8.dp))
            if (!connected) {
                Text("Connect to the hub to change these.", color = Accent.muted)
            } else {
                var ah by remember(meta.batteryAh) { mutableStateOf(if (meta.batteryAh > 0) meta.batteryAh.toString() else "") }
                var confirmFull by remember { mutableStateOf(false) }
                val ahNum = ah.toIntOrNull()
                val ahOk = ahNum != null && ahNum in 20..2000
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(ah, { v -> ah = v.filter { it.isDigit() }.take(4) }, label = { Text("Battery size") },
                        suffix = { Text("Ah") }, singleLine = true, isError = ah.isNotEmpty() && !ahOk,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                    Button(onClick = { ahNum?.let(onBatteryAh) }, enabled = ahOk && ahNum != meta.batteryAh) { Text("Save") }
                }
                Text(when {
                    ah.isNotEmpty() && !ahOk -> "20 to 2000 Ah"
                    (ahNum ?: 0) > 600 -> "Bigger than the BMPRO can handle (600 Ah), so the hub's own count is the one to trust."
                    else -> "3 x 135 Ah lithium = 405 Ah"
                }, style = MaterialTheme.typography.bodySmall, color = if (ah.isNotEmpty() && !ahOk) Accent.warn else Accent.muted)
                Spacer(Modifier.height(8.dp))
                if (!confirmFull) {
                    OutlinedButton(onClick = { confirmFull = true }) { Text("Battery is full: set to 100%") }
                } else {
                    Text("Only do this when the battery really is full.", color = Accent.warn, style = MaterialTheme.typography.bodyMedium)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { onBatteryFull(); confirmFull = false }) { Text("Set to 100%") }
                        OutlinedButton(onClick = { confirmFull = false }) { Text("Cancel") }
                    }
                }
            }
        }

        SectionCard("Touchscreen", Accent.climate) {
            if (!connected) {
                Text("Connect to the hub to change these.", color = Accent.muted)
            } else {
                if (meta.screenSleepSec >= 0) {
                    Text("Screen dims after", style = MaterialTheme.typography.labelMedium, color = Accent.muted)
                    Picker(sleepLabel(meta.screenSleepSec), (SLEEP_OPTIONS + meta.screenSleepSec).distinct().sortedBy { if (it == 0) Int.MAX_VALUE else it }.map { it to sleepLabel(it) }) {
                        onScreenSleep(it)
                    }
                    Spacer(Modifier.height(8.dp))
                }
                SwitchRow("Allow control from the internet", meta.remoteControl) {
                    onRemoteControl(it)
                }
                Text("When off, the remote dashboard can look but not switch pumps or change settings.",
                    style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            }
        }

        SectionCard("Secure mode", Accent.secure, icon = CaravanIcons.Lock) {
            Text("Arm it from Home. If the van is moved, you get 5 minutes to disarm before the full alarm. " +
                "Disarming on the touchscreen needs this code; this phone doesn't (it has its own lock screen).",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            Spacer(Modifier.height(8.dp))
            if (!connected) Text("Connect to the hub to change these.", color = Accent.muted)
            else {
                Text("Sensitivity", style = MaterialTheme.typography.labelLarge)
                Text("Used every time it's armed. Low ignores more wind rocking.",
                    style = MaterialTheme.typography.bodySmall, color = Accent.muted)
                Spacer(Modifier.height(6.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Secure.SENS.forEachIndexed { i, n ->
                        FilterChip(selected = secSens == i, onClick = { onSecureSens(i) }, label = { Text(n) })
                    }
                }
                Spacer(Modifier.height(12.dp))
                var code by remember { mutableStateOf("") }
                Text(if (meta.secCodeSet) "A code is set." else "No code yet: anyone at the touchscreen can disarm.",
                    color = if (meta.secCodeSet) Accent.secure else Accent.warn, style = MaterialTheme.typography.bodyMedium)
                Spacer(Modifier.height(6.dp))
                OutlinedTextField(code, { v -> code = v.filter { it.isDigit() }.take(8) }, label = { Text("New code (4-8 digits)") },
                    singleLine = true, visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { onSecureCode(code); code = "" }, enabled = code.length >= 4) { Text("Save code") }
                    if (meta.secCodeSet) OutlinedButton(onClick = { onSecureCode("") }) { Text("Remove code") }
                }
            }
        }

        UpdateSection(update, updateTokenSet, onSaveUpdateToken, onCheckUpdate, onUpdate, onOpenTokenPage)

        SectionCard("More setup", Accent.muted) {
            Text("Victron keys, thermometers, tank calibration, BMPRO and weather location are coming to the app next. " +
                "Until then, set them on the hub's web page.", style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            Spacer(Modifier.height(8.dp))
            FilledTonalButton(enabled = connected, onClick = onOpenWebPage) { Text("Open hub web page") }
        }

        Row {
            Text("App $appVersion" + if (meta.fw.isNotBlank()) " · hub firmware ${meta.fw}" else "",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
        }
        Spacer(Modifier.height(8.dp))
    }
}

private fun sleepLabel(sec: Int) = when {
    sec == 0 -> "Never"
    sec >= 3600 && sec % 3600 == 0 -> "${sec / 3600} h"
    sec >= 60 && sec % 60 == 0 -> "${sec / 60} min"
    else -> "$sec s"
}

