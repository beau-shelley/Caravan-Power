package com.caravan.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.OutlinedButton
import androidx.compose.ui.draw.clip
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caravan.monitor.Actions
import com.caravan.monitor.Level
import com.caravan.monitor.Levels
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
    // Extra steps as editable text: (threshold, severity).
    var levels by remember { mutableStateOf(initial.levels.map { fmtNum(it.threshold) to it.sev }) }
    var confirmDelete by remember { mutableStateOf(false) }

    val m = Metrics.byKey(metric)
    val thrVal = thr.replace(',', '.').toDoubleOrNull()
    val hysVal = hys.replace(',', '.').toDoubleOrNull()
    val levelVals = levels.map { it.first.replace(',', '.').toDoubleOrNull() }
    // Each step must be deeper than the one before it (lower for "below", higher for "above").
    val levelErrors = levelVals.mapIndexed { i, v ->
        val prev = if (i == 0) thrVal else levelVals[i - 1]
        when {
            v == null -> "Enter a number"
            prev != null && !(if (above) v > prev else v < prev) ->
                "Must be ${if (above) "above" else "below"} ${fmtNum(prev)} ${m.unit}"
            else -> null
        }
    }
    val valid = thrVal != null && hysVal != null && hysVal >= 0 && levelErrors.all { it == null }

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

                Field(if (levels.isEmpty()) "Severity" else "Severity at ${thr.ifBlank { "?" }} ${m.unit}") {
                    SeverityChips(sev) { sev = it }
                }

                // Stepped levels: e.g. warning at 30 %, alarm at 15 %, critical at 5 %.
                levels.forEachIndexed { i, (t, s) ->
                    Column(
                        Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(Surfaces.card)
                            .padding(horizontal = 8.dp, vertical = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("Then at level ${i + 2}", fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                            TextButton(onClick = { levels = levels.filterIndexed { j, _ -> j != i } }) { Text("Remove") }
                        }
                        NumberField("${if (above) "Above" else "Below"} (${m.unit})", t,
                            { v -> levels = levels.mapIndexed { j, l -> if (j == i) v to l.second else l } }, m.step,
                            error = levelErrors[i])
                        SeverityChips(s) { v -> levels = levels.mapIndexed { j, l -> if (j == i) l.first to v else l } }
                    }
                }
                if (levels.size < Levels.MAX) {
                    OutlinedButton(onClick = {
                        val prev = levelVals.lastOrNull() ?: thrVal ?: defaultThreshold(metric)
                        val gap = maxOf(m.step, Math.abs(prev) / 2)
                        val next = Math.round((if (above) prev + gap else prev - gap) / m.step) * m.step
                        val prevSev = levels.lastOrNull()?.second ?: sev
                        levels = levels + (fmtNum(next) to minOf(prevSev + 1, Severity.CRIT))
                    }, modifier = Modifier.fillMaxWidth()) { Text("+ Add a level") }
                }
                Hint(
                    if (levels.isEmpty()) "Add a level to step up, e.g. a warning at 30 %, an alarm at 15 % and critical at 5 %."
                    else "Each level alerts once. It won't alert again until the value recovers by the amount above."
                )

                SwitchRow("Send a phone notification", notify) { notify = it }

                Field(if (levels.isEmpty()) "Also do this automatically" else "At the last level, also") {
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
                            levels = levels.mapIndexed { i, l -> Level(levelVals[i]!!, l.second) },
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



@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SeverityChips(sev: Int, onPick: (Int) -> Unit) {
    // Four equal chips so "Critical" still fits inside a level card; the label carries the colour.
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(Severity.INFO, Severity.WARN, Severity.ALARM, Severity.CRIT).forEach { s ->
            FilterChip(
                selected = sev == s, onClick = { onPick(s) }, modifier = Modifier.weight(1f),
                label = {
                    Text(Severity.label(s), maxLines = 1, color = sevColor(s), fontWeight = FontWeight.SemiBold,
                        fontSize = 13.sp, letterSpacing = 0.sp,
                        textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
                },
            )
        }
    }
}

@Composable
private fun NumberField(label: String, value: String, onChange: (String) -> Unit, step: Double, help: String? = null,
                        error: String? = null) {
    fun bump(d: Double) {
        val v = value.replace(',', '.').toDoubleOrNull() ?: 0.0
        onChange(fmtNum(Math.round((v + d) * 100) / 100.0))
    }
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value, onChange, label = { Text(label) }, singleLine = true,
                isError = error != null || value.replace(',', '.').toDoubleOrNull() == null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            FilledTonalIconButton(onClick = { bump(-step) }) { Text("−") }
            FilledTonalIconButton(onClick = { bump(step) }) { Text("+") }
        }
        if (error != null) Text(error, style = MaterialTheme.typography.bodySmall, color = Accent.alarm)
        else if (help != null) Text(help, style = MaterialTheme.typography.bodySmall, color = Accent.muted)
    }
}

