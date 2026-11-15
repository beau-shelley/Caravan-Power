package com.caravan.monitor.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.caravan.monitor.Actions
import com.caravan.monitor.CaravanApp
import com.caravan.monitor.HubClient
import com.caravan.monitor.Rule
import com.caravan.monitor.Severity
import org.json.JSONObject

@Composable
fun AlertsScreen() {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    val rules by hub.rules.collectAsStateWithLifecycle()
    val meta by hub.meta.collectAsStateWithLifecycle()
    val status by hub.status.collectAsStateWithLifecycle()
    val connected = status.conn == HubClient.Conn.CONNECTED

    var editing by remember { mutableStateOf<Rule?>(null) }
    var menu by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }

    Box(Modifier.fillMaxSize()) {
        LazyColumn(
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 96.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Alerts", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold,
                        modifier = Modifier.weight(1f))
                    Box {
                        IconButton(onClick = { menu = true }) { Icon(Icons.Filled.MoreVert, "More") }
                        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                            DropdownMenuItem(text = { Text("Restore default rules") }, enabled = connected,
                                onClick = { menu = false; confirmReset = true })
                        }
                    }
                }
            }
            item { ConnectionStrip() }

            val alerts = state?.alerts.orEmpty()
            item {
                SectionCard("Active now", if (alerts.any { !it.ack }) sevColor(alerts.maxOf { it.sev }) else Accent.battery) {
                    if (alerts.isEmpty()) Text("All clear", color = Accent.muted)
                    alerts.forEach { a -> AlertRow(a, rules.firstOrNull { it.id == a.id }?.name) }
                    if (alerts.any { !it.ack }) {
                        Spacer(Modifier.height(4.dp))
                        Button(onClick = { hub.send("ack") }) { Text("Acknowledge all") }
                    }
                }
            }

            item {
                Text("RULES", style = MaterialTheme.typography.labelMedium, color = Accent.muted,
                    modifier = Modifier.padding(top = 8.dp, start = 4.dp))
                Text("Tap a rule to change it. The hub checks these all the time, even with your phone away.",
                    style = MaterialTheme.typography.bodySmall, color = Accent.muted, modifier = Modifier.padding(start = 4.dp))
            }
            if (rules.isEmpty()) item {
                Text(if (connected) "No rules yet. Add one below." else "Connect to the hub to see its rules.", color = Accent.muted)
            }
            items(rules, key = { it.id }) { r ->
                RuleRow(r, meta, enabled = connected, onClick = { editing = r }) { on ->
                    hub.send(JSONObject().put("c", "rule_set").put("rule", r.copy(enabled = on).toJson()))
                }
            }
        }

        if (connected) {
            ExtendedFloatingActionButton(
                onClick = { editing = Rule(name = "", metric = "bat_soc", above = false, threshold = 30.0, hysteresis = 5.0, delaySec = 60) },
                icon = { Icon(Icons.Filled.Add, null) },
                text = { Text("New rule") },
                modifier = Modifier.align(Alignment.BottomEnd).padding(16.dp),
            )
        }
    }

    editing?.let { r ->
        RuleEditor(
            initial = r,
            meta = meta,
            onDismiss = { editing = null },
            onSave = { hub.send(JSONObject().put("c", "rule_set").put("rule", it.toJson())); editing = null },
            onDelete = if (r.id != 0) ({ hub.send("rule_del", "id" to r.id); editing = null }) else null,
        )
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
private fun RuleRow(r: Rule, meta: com.caravan.monitor.Meta, enabled: Boolean, onClick: () -> Unit, onToggle: (Boolean) -> Unit) {
    SectionCard(Severity.label(r.sev), sevColor(r.sev), Modifier.clickable(enabled = enabled, onClick = onClick)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(r.name.ifBlank { "(unnamed)" }, fontWeight = FontWeight.SemiBold,
                    color = if (r.enabled) MaterialTheme.colorScheme.onSurface else Accent.muted)
                Text(r.describe(meta), style = MaterialTheme.typography.bodySmall, color = Accent.muted)
                val extras = buildList {
                    if (!r.notify) add("no phone alert")
                    if (r.action != "none") add("then: ${Actions.label(r.action).lowercase()}")
                }
                if (extras.isNotEmpty()) Text(extras.joinToString(" · "), style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            }
            Spacer(Modifier.width(8.dp))
            Switch(checked = r.enabled, onCheckedChange = onToggle, enabled = enabled)
        }
    }
}
