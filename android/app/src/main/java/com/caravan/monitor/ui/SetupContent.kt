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
    /** Opens the admin settings (asks for the admin login first if needed). */
    onOpenAdmin: () -> Unit = {},
    adminUnlocked: Boolean = false,
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

        SectionCard("Admin: van settings", Accent.lime, icon = CaravanIcons.Lock) {
            Text("Weather location, battery size, Victron keys, thermometers, diesel heater, tank calibration, " +
                "touchscreen, secure mode code and BMPRO. Works at the van and from away (through the cloud).",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            Spacer(Modifier.height(8.dp))
            Button(onClick = onOpenAdmin) { Text(if (adminUnlocked) "Open admin settings" else "Admin login") }
        }

        UpdateSection(update, updateTokenSet, onSaveUpdateToken, onCheckUpdate, onUpdate, onOpenTokenPage)

        Row {
            Text("App $appVersion" + if (meta.fw.isNotBlank()) " · hub firmware ${meta.fw}" else "",
                style = MaterialTheme.typography.bodySmall, color = Accent.muted)
        }
        Spacer(Modifier.height(8.dp))
    }
}
