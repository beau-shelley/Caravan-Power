package com.caravan.monitor.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
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
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.caravan.monitor.Actions
import com.caravan.monitor.Meta
import com.caravan.monitor.Metrics
import com.caravan.monitor.Rule
import com.caravan.monitor.Severity
import com.caravan.monitor.fmtNum
import com.caravan.monitor.formatDelay

private val DELAYS = listOf(0, 10, 30, 60, 120, 300, 600, 900, 1800, 3600)

/** The rule form. Shown in a dialog on the phone (RuleEditorDialog in Screens.kt). */
@OptIn(ExperimentalMaterial3Api::class) // FilterChip is still experimental in older Material 3
@Composable
fun RuleEditorCard(
    initial: Rule,
    meta: Meta,
    onDismiss: () -> Unit,
    onSave: (Rule) -> Unit,
    onDelete: (() -> Unit)?,
    modifier: Modifier = Modifier,
    scrollable: Boolean = true,
) {
    var name by remember { mutableStateOf(initial.name) }
    var metric by remember { mutableStateOf(initial.metric) }
    var above by remember { mutableStateOf(initial.above) }
    var thr by remember { mutableStateOf(fmtNum(initial.threshold)) }
    var hys by remember { mutableStateOf(fmtNum(initial.hysteresis)) }
    var delay by remember { mutableStateOf(initial.delaySec) }
    var sev by remember { mutableStateOf(initial.sev) }
    var notify by remember { mutableStateOf(initial.notify) }
    var action by remember { mutableStateOf(initial.action) }
    var enabled by remember { mutableStateOf(initial.enabled) }
    var confirmDelete by remember { mutableStateOf(false) }

    val m = Metrics.byKey(metric)
    val thrVal = thr.replace(',', '.').toDoubleOrNull()
    val hysVal = hys.replace(',', '.').toDoubleOrNull()
    val valid = thrVal != null && hysVal != null && hysVal >= 0

    Surface(
            modifier.fillMaxWidth(),
            shape = RoundedCornerShape(20.dp),
            color = Surfaces.raised,
        ) {
            Column((if (scrollable) Modifier.verticalScroll(rememberScrollState()) else Modifier).padding(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                Text(if (initial.id == 0) "New alert rule" else "Edit alert rule",
                    style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)

                OutlinedTextField(name, { name = it.take(27) }, label = { Text("Name (shown in the notification)") },
                    singleLine = true, modifier = Modifier.fillMaxWidth(),
                    placeholder = { Text(Metrics.label(metric, meta)) })

                Field("Watch") {
                    Picker(
                        value = Metrics.label(metric, meta),
                        options = Metrics.all.map { it.key to Metrics.label(it.key, meta) },
                        onPick = { k ->
                            if (k != metric) {
                                metric = k
                                // Sensible starting numbers for the new metric.
                                thr = fmtNum(defaultThreshold(k)); hys = fmtNum(Metrics.byKey(k).step)
                            }
                        },
                    )
                }

                Field("Alert when it goes") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilterChip(selected = !above, onClick = { above = false }, label = { Text("Below") })
                        FilterChip(selected = above, onClick = { above = true }, label = { Text("Above") })
                    }
                }

                NumberField("Threshold (${m.unit})", thr, { thr = it }, m.step)
                NumberField("Clear after recovering by (${m.unit})", hys, { hys = it }, m.step,
                    help = "Stops the alert flapping on and off around the threshold.")

                Field("Only if it lasts") {
                    Picker(formatDelay(delay), (DELAYS + delay).distinct().sorted().map { it to formatDelay(it) }) { delay = it }
                }

                Field("Severity") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf(Severity.INFO, Severity.WARN, Severity.ALARM).forEach { s ->
                            FilterChip(selected = sev == s, onClick = { sev = s }, label = { Text(Severity.label(s)) })
                        }
                    }
                }

                SwitchRow("Send a phone notification", notify) { notify = it }

                Field("Also do this automatically") {
                    val opts = Actions.visible.toMutableList()
                    if (opts.none { it.first == action }) opts.add(action to Actions.label(action))
                    Picker(Actions.label(action), opts) { action = it }
                }

                SwitchRow("Rule switched on", enabled) { enabled = it }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (onDelete != null) TextButton(onClick = { confirmDelete = true }) { Text("Delete", color = Accent.alarm) }
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = onDismiss) { Text("Cancel") }
                    Spacer(Modifier.width(8.dp))
                    Button(enabled = valid, onClick = {
                        onSave(initial.copy(
                            name = name.trim().ifBlank { Metrics.label(metric, meta) }, metric = metric, above = above,
                            threshold = thrVal!!, hysteresis = hysVal!!, delaySec = delay, sev = sev,
                            notify = notify, action = action, enabled = enabled,
                        ))
                    }) { Text("Save") }
                }
            }
        }

    if (confirmDelete && onDelete != null) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete this rule?") },
            confirmButton = { TextButton(onClick = { confirmDelete = false; onDelete() }) { Text("Delete") } },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("Cancel") } },
        )
    }
}

private fun defaultThreshold(key: String) = when (key) {
    "bat_v" -> 12.0; "bat_soc" -> 30.0; "bat_a", "load_a" -> 30.0; "charge_a" -> 40.0
    "solar_w", "solar1_w", "solar2_w" -> 50.0
    "temp_in" -> 35.0; "temp_out" -> 40.0; "temp_fridge" -> 8.0
    "tank1", "tank2", "tank3" -> 20.0
    "wx_gust" -> 60.0; "wx_rain" -> 70.0
    "gas" -> 20.0
    else -> 0.0
}



@Composable
private fun NumberField(label: String, value: String, onChange: (String) -> Unit, step: Double, help: String? = null) {
    fun bump(d: Double) {
        val v = value.replace(',', '.').toDoubleOrNull() ?: 0.0
        onChange(fmtNum(Math.round((v + d) * 100) / 100.0))
    }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value, onChange, label = { Text(label) }, singleLine = true,
                isError = value.replace(',', '.').toDoubleOrNull() == null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            FilledTonalIconButton(onClick = { bump(-step) }) { Text("−") }
            FilledTonalIconButton(onClick = { bump(step) }) { Text("+") }
        }
        if (help != null) Text(help, style = MaterialTheme.typography.bodySmall, color = Accent.muted)
    }
}

