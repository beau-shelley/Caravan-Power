package com.caravan.monitor.ui

// Admin: every hub setting the hub's web page (Setup tab) can change, behind
// the admin login. Works on the caravan's Wi-Fi and through the cloud.
// Like the other *Content files, nothing here touches Android APIs.

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.MyLocation
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.caravan.monitor.Conn
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.Found
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import com.caravan.monitor.Place
import com.caravan.monitor.Secure

private val SLEEP_OPTIONS = listOf(10, 20, 30, 60, 300, 900, 1800, 3600, 0)
private val MAC = Regex("^([0-9a-f]{2}:){5}[0-9a-f]{2}$")

/** What the hub (or the phone) found when asked: filled in from the hub's replies. */
data class AdminFinds(
    val victron: List<Found>? = null,
    val bleTemps: List<Found>? = null,
    val wiredTemps: List<Found>? = null,
    val heaters: List<Found>? = null,
    val heaterLog: String? = null,
    val bmproLog: String? = null,
    val places: List<Place>? = null,
    /** Phone location or town search in progress. */
    val locBusy: Boolean = false,
    val locMsg: String = "",
)

@OptIn(ExperimentalMaterial3Api::class) // FilterChip is still experimental in older Material 3
@Composable
fun AdminContent(
    state: HubState?,
    meta: Meta,
    status: ConnStatus,
    finds: AdminFinds,
    /** Send a command to the hub, e.g. send("victron_set", mapOf("i" to 0, ...)). */
    send: (String, Map<String, Any>) -> Unit,
    onBack: () -> Unit,
    onLogout: () -> Unit,
    onUsePhoneLocation: () -> Unit,
    onSearchTown: (String) -> Unit,
    onPickPlace: (Place) -> Unit,
    onOpenAlerts: () -> Unit,
    onOpenLevel: () -> Unit,
    onOpenWebPage: (() -> Unit)? = null,
) {
    val connected = status.conn == Conn.CONNECTED
    val blocked = status.cloud && !meta.remoteControl
    fun cmd(c: String, vararg kv: Pair<String, Any>) = send(c, mapOf(*kv))

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.Filled.ArrowBack, "Back") }
            Text("Admin", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold,
                modifier = Modifier.weight(1f))
            TextButton(onClick = onLogout) { Text("Log out") }
        }
        ConnectionStrip(status)
        when {
            !connected -> Banner("Not connected to the hub. Settings can't be read or changed until it's back.", Accent.warn)
            blocked -> Banner("You're away from the van and \"Allow control from the internet\" is off, so the hub " +
                "will refuse changes. Turn it on below while you're on the caravan's Wi-Fi.", Accent.warn)
            status.cloud -> Banner("Through the cloud: changes go to the van over its internet link.", Accent.solar)
        }

        // ---------------------------------------------------------------- weather location
        SectionCard("Weather location", Accent.water) {
            val now = when {
                meta.locSrc == 0 && meta.locName.isBlank() -> "Not set yet. The forecast appears once it is."
                else -> "Now: ${meta.locName.ifBlank { "unnamed spot" }}" +
                    (Place.SRC_LABEL.getOrNull(meta.locSrc)?.takeIf { it.isNotBlank() }?.let { " (from $it)" } ?: "")
            }
            Text(now, fontWeight = FontWeight.SemiBold)
            if (meta.locSrc == 1) Hint("The hub's GPS sets this by itself while it has a fix.")
            Spacer(Modifier.height(8.dp))
            Button(onClick = onUsePhoneLocation, enabled = connected && !finds.locBusy, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Filled.MyLocation, null)
                Spacer(Modifier.width(8.dp))
                Text("Use this phone's location")
            }
            Spacer(Modifier.height(8.dp))
            var q by remember { mutableStateOf("") }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(q, { q = it }, label = { Text("Town or postcode") }, singleLine = true,
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(onSearch = { if (q.length >= 2) onSearchTown(q) }),
                    modifier = Modifier.weight(1f))
                FilledTonalButton(onClick = { onSearchTown(q) }, enabled = q.trim().length >= 2 && !finds.locBusy) {
                    Icon(Icons.Filled.Search, "Search")
                }
            }
            if (finds.locBusy) {
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.height(18.dp).width(18.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp)); Hint("Looking...")
                }
            }
            if (finds.locMsg.isNotBlank()) Text(finds.locMsg, color = Accent.warn, style = MaterialTheme.typography.bodySmall)
            finds.places?.let { list ->
                if (list.isEmpty()) Hint("No towns found. Try fewer letters or the postcode.")
                list.forEach { p -> PlaceRow(p, enabled = connected) { onPickPlace(p) } }
            }
            Spacer(Modifier.height(4.dp))
            Hint("Quick pick")
            PlaceRow(Place.HOME, enabled = connected) { onPickPlace(Place.HOME) }
        }

        // ---------------------------------------------------------------- battery
        SectionCard("Battery", Accent.battery) {
            Hint("The hub works out the battery % and time left itself, by counting the amps in and out " +
                "against this size. (The BMPRO's own % stops at 600 Ah.)")
            Spacer(Modifier.height(8.dp))
            var ah by remember(meta.batteryAh) { mutableStateOf(if (meta.batteryAh > 0) meta.batteryAh.toString() else "") }
            var confirmFull by remember { mutableStateOf(false) }
            val ahNum = ah.toIntOrNull()
            val ahOk = ahNum != null && ahNum in 20..2000
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(ah, { v -> ah = v.filter { it.isDigit() }.take(4) }, label = { Text("Battery size") },
                    suffix = { Text("Ah") }, singleLine = true, isError = ah.isNotEmpty() && !ahOk,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f))
                Button(onClick = { ahNum?.let { cmd("battery_set", "cap" to it) } },
                    enabled = connected && ahOk && ahNum != meta.batteryAh) { Text("Save") }
            }
            Hint(if (ah.isNotEmpty() && !ahOk) "20 to 2000 Ah" else "3 x 135 Ah lithium = 405 Ah")
            Spacer(Modifier.height(8.dp))
            if (!confirmFull) {
                OutlinedButton(onClick = { confirmFull = true }, enabled = connected) { Text("Battery is full: set to 100%") }
            } else {
                Text("Only do this when the battery really is full.", color = Accent.warn)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { cmd("soc_set", "soc" to 100); confirmFull = false }) { Text("Set to 100%") }
                    OutlinedButton(onClick = { confirmFull = false }) { Text("Cancel") }
                }
            }
        }

        // ---------------------------------------------------------------- victron
        SectionCard("Victron solar controllers", Accent.solar) {
            Hint("In VictronConnect open each controller, then cog > Product info > Instant readout via Bluetooth: " +
                "turn it on, tap Show next to Instant readout details, and copy the encryption key here. " +
                "The address is optional.")
            val vicAddr = remember(meta.solarNames.size) { mutableStateListOf(*Array(meta.solarNames.size) { "" }) }
            meta.solarNames.forEachIndexed { i, name ->
                Spacer(Modifier.height(10.dp))
                Text(name, fontWeight = FontWeight.SemiBold)
                var key by remember { mutableStateOf("") }
                val addr = vicAddr[i]
                val keyClean = key.filter { it.isDigit() || it.lowercaseChar() in 'a'..'f' }
                val addrOk = addr.isBlank() || MAC.matches(addr.trim().lowercase())
                OutlinedTextField(key, { key = it.trim() }, label = { Text("Encryption key (32 characters)") }, singleLine = true,
                    isError = key.isNotEmpty() && keyClean.length != 32,
                    supportingText = { Text("${keyClean.length} / 32") },
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(addr, { vicAddr[i] = it.trim() }, label = { Text("Bluetooth address (optional)") }, singleLine = true,
                    placeholder = { Text("aa:bb:cc:dd:ee:ff") }, isError = !addrOk, modifier = Modifier.fillMaxWidth())
                Button(onClick = {
                    cmd("victron_set", "i" to i, "key" to keyClean.lowercase(), "addr" to addr.trim().lowercase(), "src" to 0)
                    key = ""
                }, enabled = connected && keyClean.length == 32 && addrOk) { Text("Save $name") }
            }
            Spacer(Modifier.height(10.dp))
            FilledTonalButton(onClick = { cmd("victron_seen") }, enabled = connected) { Text("Find nearby") }
            finds.victron?.let { list ->
                if (list.isEmpty()) Hint("No Victron broadcasts heard yet. Check Instant readout is on.")
                list.forEach { d ->
                    Spacer(Modifier.height(6.dp))
                    Text("${d.type.ifBlank { "Victron" }}  ${d.addr}  ${d.rssi} dBm", style = MaterialTheme.typography.bodySmall)
                    if (d.slot >= 0) Text("Decoding as ${meta.solarNames.getOrElse(d.slot) { "?" }}",
                        color = Accent.battery, style = MaterialTheme.typography.bodySmall)
                    else Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        meta.solarNames.forEachIndexed { i, n ->
                            TextButton(onClick = { vicAddr[i] = d.addr }) { Text("Use for $n") }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------------- thermometers
        SectionCard("Thermometers", Accent.climate) {
            Hint("Bluetooth thermometers (ThermoPro TP357, Govee H5075, Shelly BLU H&T, Xiaomi with pvvx firmware) " +
                "or wired DS18B20 probes. Warm one in your hand to tell which is which.")
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = { cmd("ble_sensors") }, enabled = connected) { Text("Find Bluetooth") }
                OutlinedButton(onClick = { cmd("sensors") }, enabled = connected) { Text("Wired probes") }
            }
            finds.bleTemps?.let { list ->
                if (list.isEmpty()) Hint("No Bluetooth thermometers heard yet. Give it 30 s and try again.")
                list.forEach { s ->
                    SensorRow("${s.c?.let { fmtDeg(it) + "°  " } ?: ""}${s.name.ifBlank { s.type }}",
                        "${s.type} · ${s.addr} · ${s.rssi} dBm" + if (s.batt >= 0) " · batt ${s.batt}%" else "",
                        s.role, meta.tempNames, "Not used") { role ->
                        if (role >= 0) { cmd("temp_set", "role" to role, "src" to 1, "addr" to s.addr); cmd("ble_sensors") }
                    }
                }
            }
            finds.wiredTemps?.let { list ->
                if (list.isEmpty()) Hint("No wired probes found.")
                list.forEach { s ->
                    SensorRow("${s.c?.let { fmtDeg(it) + "°  " } ?: ""}Wired probe", s.addr, s.role, meta.tempNames, "Unassigned") { role ->
                        if (role >= 0) { cmd("sensor_assign", "addr" to s.addr, "role" to role); cmd("sensors") }
                    }
                }
            }
        }

        // ---------------------------------------------------------------- heater
        SectionCard("Diesel heater", Accent.heat) {
            Hint("Bluetooth works with heaters that use the AirHeaterBLE app. Close that app first: the heater " +
                "takes one Bluetooth connection at a time. The 433 MHz option isn't built yet.")
            Spacer(Modifier.height(8.dp))
            var link by remember(state?.htr?.link) { mutableStateOf(state?.htr?.link ?: 0) }
            var addr by remember { mutableStateOf("") }
            var pass by remember { mutableStateOf("1234") }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf("None", "Bluetooth", "433 MHz").forEachIndexed { i, n ->
                    FilterChip(selected = link == i, onClick = { link = i }, label = { Text(n) })
                }
            }
            val addrOk = MAC.matches(addr.trim().lowercase())
            if (link == 1) {
                OutlinedTextField(addr, { addr = it.trim() }, label = { Text("Bluetooth address") }, singleLine = true,
                    placeholder = { Text("aa:bb:cc:dd:ee:ff") }, isError = addr.isNotEmpty() && !addrOk, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(pass, { v -> pass = v.filter { it.isDigit() }.take(4) }, label = { Text("Passkey (from the app)") },
                    singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth())
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = {
                cmd("heater_set", "link" to link, "addr" to (if (link == 1) addr.trim().lowercase() else ""), "pass" to (pass.toIntOrNull() ?: 1234))
            }, enabled = connected && (link != 1 || addrOk)) { Text("Save heater") }
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = { cmd("heater_seen") }, enabled = connected) { Text("Find nearby") }
                OutlinedButton(onClick = { cmd("heater_log") }, enabled = connected) { Text("Show log") }
            }
            finds.heaters?.let { list ->
                if (list.isEmpty()) Hint("No heater heard yet. Make sure it has power and the phone app is closed, then try again in 30 s.")
                list.forEach { d ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(d.name.ifBlank { "Unnamed" }, fontWeight = FontWeight.SemiBold)
                            Hint("${d.addr} · ${d.rssi} dBm")
                        }
                        TextButton(onClick = { addr = d.addr; link = 1 }) { Text("Use") }
                    }
                }
            }
            finds.heaterLog?.let { LogText(it) }
        }

        // ---------------------------------------------------------------- tanks
        if (meta.tankCal) SectionCard("Tank calibration", Accent.water) {
            Hint("Fill or empty a tank, then press the matching button to store the sender reading.")
            var ask by remember { mutableStateOf<Pair<Int, String>?>(null) }
            meta.tankNames.forEachIndexed { i, n ->
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(n, modifier = Modifier.weight(1f))
                    OutlinedButton(onClick = { ask = i to "empty" }, enabled = connected) { Text("Empty") }
                    OutlinedButton(onClick = { ask = i to "full" }, enabled = connected) { Text("Full") }
                }
                ask?.takeIf { it.first == i }?.let { (_, pt) ->
                    Text("Store the reading now as ${pt.uppercase()} for $n?", color = Accent.warn)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { cmd("tank_cal", "i" to i, "point" to pt); ask = null }) { Text("Store") }
                        OutlinedButton(onClick = { ask = null }) { Text("Cancel") }
                    }
                }
            }
        }

        // ---------------------------------------------------------------- touchscreen
        SectionCard("Touchscreen", Accent.climate) {
            if (meta.screenSleepSec >= 0) {
                Text("Screen dims after", style = MaterialTheme.typography.labelMedium, color = Accent.muted)
                Picker(sleepLabel(meta.screenSleepSec),
                    (SLEEP_OPTIONS + meta.screenSleepSec).distinct().sortedBy { if (it == 0) Int.MAX_VALUE else it }.map { it to sleepLabel(it) }) {
                    cmd("display_set", "sleep" to it)
                }
                Spacer(Modifier.height(8.dp))
            }
            SwitchRow("Allow control from the internet", meta.remoteControl, enabled = connected && !status.cloud) {
                cmd("remote_control", "on" to it)
            }
            Hint(if (status.cloud) "Only changeable on the caravan's Wi-Fi, so nobody can switch it on from outside."
                else "When off, the app and dashboard can look from away but not change settings or switch things.")
        }

        // ---------------------------------------------------------------- secure mode
        SectionCard("Secure mode", Accent.secure, icon = CaravanIcons.Lock) {
            Hint("If the van is moved while armed you get 5 minutes to disarm before the full alarm. " +
                "Disarming on the touchscreen needs this code; the phone doesn't.")
            Spacer(Modifier.height(8.dp))
            Text("Sensitivity", style = MaterialTheme.typography.labelLarge)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Secure.SENS.forEachIndexed { i, n ->
                    FilterChip(selected = (state?.sec?.sens ?: 1) == i, onClick = { cmd("secure", "sens" to i) }, label = { Text(n) })
                }
            }
            Spacer(Modifier.height(8.dp))
            var code by remember { mutableStateOf("") }
            Text(if (meta.secCodeSet) "A code is set." else "No code yet: anyone at the touchscreen can disarm.",
                color = if (meta.secCodeSet) Accent.secure else Accent.warn)
            OutlinedTextField(code, { v -> code = v.filter { it.isDigit() }.take(8) }, label = { Text("New code (4-8 digits)") },
                singleLine = true, visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword), modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { cmd("secure_code", "code" to code); code = "" }, enabled = connected && code.length >= 4) { Text("Save code") }
                if (meta.secCodeSet) OutlinedButton(onClick = { cmd("secure_code", "code" to "") }, enabled = connected) { Text("Remove code") }
            }
        }

        // ---------------------------------------------------------------- BMPRO
        SectionCard("BMPRO OdysseyLink", Accent.muted) {
            Hint("The OdysseyLink's Bluetooth address. The hub remembers it and reconnects after a restart.")
            var addr by remember { mutableStateOf("") }
            val ok = MAC.matches(addr.trim().lowercase())
            OutlinedTextField(addr, { addr = it.trim() }, label = { Text("Bluetooth address") }, singleLine = true,
                placeholder = { Text("aa:bb:cc:dd:ee:ff") }, isError = addr.isNotEmpty() && !ok, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { cmd("bmpro_connect", "addr" to addr.trim().lowercase()) }, enabled = connected && ok) { Text("Connect") }
                OutlinedButton(onClick = { cmd("bmpro_log") }, enabled = connected) { Text("Show log") }
            }
            finds.bmproLog?.let { LogText(it) }
        }

        // ---------------------------------------------------------------- elsewhere
        SectionCard("Also set up here", Accent.lime) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = onOpenAlerts) { Text("Alert rules") }
                FilledTonalButton(onClick = onOpenLevel) { Text("Level and van size") }
            }
            if (onOpenWebPage != null) {
                Spacer(Modifier.height(4.dp))
                TextButton(onClick = onOpenWebPage) { Text("Open the hub's web page") }
            }
        }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun Banner(text: String, color: androidx.compose.ui.graphics.Color) {
    SectionCard("Note", color, compact = true) { Text(text, style = MaterialTheme.typography.bodyMedium) }
}

@Composable
private fun PlaceRow(p: Place, enabled: Boolean, onPick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Column(Modifier.weight(1f)) {
            Text(p.name, fontWeight = FontWeight.SemiBold)
            if (p.detail.isNotBlank()) Hint(p.detail)
        }
        OutlinedButton(onClick = onPick, enabled = enabled) { Text("Use") }
    }
}

@Composable
private fun SensorRow(title: String, detail: String, role: Int, names: List<String>, none: String, onRole: (Int) -> Unit) {
    Spacer(Modifier.height(8.dp))
    Text(title, fontWeight = FontWeight.SemiBold)
    Hint(detail)
    Picker(names.getOrNull(role) ?: none, listOf(-1 to none) + names.mapIndexed { i, n -> i to n }) { onRole(it) }
}

@Composable
private fun LogText(text: String) {
    Spacer(Modifier.height(8.dp))
    Text(text.ifBlank { "(empty)" }, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall,
        color = Accent.muted)
}

fun sleepLabel(sec: Int) = when {
    sec == 0 -> "Never"
    sec >= 3600 && sec % 3600 == 0 -> "${sec / 3600} h"
    sec >= 60 && sec % 60 == 0 -> "${sec / 60} min"
    else -> "$sec s"
}
