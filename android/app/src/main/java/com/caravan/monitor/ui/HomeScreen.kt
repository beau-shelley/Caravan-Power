package com.caravan.monitor.ui

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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.caravan.monitor.CaravanApp
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import java.util.Locale

@Composable
fun HomeScreen(onOpenAlerts: () -> Unit, onOpenSetup: () -> Unit) {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    val meta by hub.meta.collectAsStateWithLifecycle()
    val rules by hub.rules.collectAsStateWithLifecycle()

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Caravan", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
        ConnectionStrip(onOpenSetup)
        val s = state
        if (s == null) {
            Text("Waiting for the hub…", color = Accent.muted)
            return@Column
        }

        val unacked = s.alerts.filter { !it.ack }
        if (s.alerts.isNotEmpty()) {
            SectionCard("Alerts", if (unacked.isNotEmpty()) sevColor(unacked.maxOf { it.sev }) else Accent.muted) {
                s.alerts.take(3).forEach { a -> AlertRow(a, rules.firstOrNull { it.id == a.id }?.name) }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (unacked.isNotEmpty()) Button(onClick = { hub.send("ack") }) { Text("Acknowledge") }
                    FilledTonalButton(onClick = onOpenAlerts) { Text("All alerts") }
                }
            }
        }

        BatteryCard(s)
        if (s.bat.on && s.bat.acOn) MainsCard(s)
        SolarCard(s, meta)
        TanksCard(s, meta)
        TempsCard(s, meta)
        PumpsCard(s, meta)
        if (s.wx.ok) {
            SectionCard("Weather${if (s.wx.place.isNotBlank()) " · ${s.wx.place}" else ""}", Accent.climate) {
                Row(Modifier.fillMaxWidth()) {
                    Stat("Now", "${fmtDeg(s.wx.tempC)} °C", Modifier.weight(1f),
                        valueColor = tempColor(1, s.wx.tempC, MaterialTheme.colorScheme.onSurface))
                    Stat("Wind", "${s.wx.windKmh.toInt()} km/h", Modifier.weight(1f))
                    Stat("Gusts", "${s.wx.gustKmh.toInt()} km/h", Modifier.weight(1f))
                }
            }
        }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun BatteryCard(s: HubState) {
    val b = s.bat
    SectionCard("Battery", Accent.battery) {
        if (!b.on) { Text("Offline", color = Accent.muted); return@SectionCard }
        Row(verticalAlignment = Alignment.Bottom) {
            Text("${b.soc.toInt()}%", fontSize = 44.sp, fontWeight = FontWeight.Bold, color = socColor(b.soc))
            Spacer(Modifier.width(12.dp))
            Column(Modifier.padding(bottom = 8.dp)) {
                Text("${fmt2(b.v)} V  ·  ${signed(b.a)} A", style = MaterialTheme.typography.bodyLarge)
                Text(ttgText(b.a, b.ttgMin), style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            }
        }
        Spacer(Modifier.height(6.dp))
        Bar(b.soc / 100.0, socColor(b.soc))
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth()) {
            Stat("Solar", "${fmt1(b.solarA)} A", Modifier.weight(1f))
            Stat("BMPRO", "${fmt1(b.chargerA)} A", Modifier.weight(1f))
            Stat("Load", "${fmt1(b.loadA)} A", Modifier.weight(1f))
        }
    }
}

// Only shown while the hub detects bulk mains charging (it debounces, so this
// doesn't flicker). The figure is worked out, not measured.
@Composable
private fun MainsCard(s: HubState) {
    val b = s.bat
    SectionCard("Mains charging", Accent.mains) {
        Row(verticalAlignment = Alignment.Bottom) {
            Text("${b.acA.toInt()} A", fontSize = 28.sp, fontWeight = FontWeight.Bold, color = Accent.mains)
            Spacer(Modifier.width(12.dp))
            Text(
                "${(b.acA * (if (b.v > 0) b.v else 13.6)).toInt()} W · bulk · estimated",
                Modifier.padding(bottom = 4.dp), style = MaterialTheme.typography.bodySmall, color = Accent.muted,
            )
        }
        Spacer(Modifier.height(6.dp))
        Bar(b.acA / 120.0, Accent.mains)  // 2 x 60 A chargers
    }
}

@Composable
private fun SolarCard(s: HubState, meta: Meta) {
    val total = s.sol.filter { it.on }.sumOf { it.w }
    SectionCard("Solar", Accent.solar) {
        Text("${total.toInt()} W", fontSize = 28.sp, fontWeight = FontWeight.Bold, color = Accent.solar)
        Spacer(Modifier.height(6.dp))
        s.sol.forEachIndexed { i, p ->
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                Text(meta.solarNames.getOrElse(i) { "Solar ${i + 1}" }, Modifier.weight(1f))
                Text(
                    if (p.on) "${p.w.toInt()} W · ${fmt2(p.yieldKwh)} kWh today" else "offline",
                    color = if (p.on) MaterialTheme.colorScheme.onSurface else Accent.muted,
                )
            }
        }
    }
}

@Composable
private fun TanksCard(s: HubState, meta: Meta) {
    if (s.tank.none { it.on }) return
    SectionCard("Water", Accent.water) {
        s.tank.forEachIndexed { i, t ->
            if (!t.on) return@forEachIndexed
            Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                Text(meta.tankNames.getOrElse(i) { "Tank ${i + 1}" }, Modifier.weight(1f))
                Text("${t.pct.toInt()}%")
            }
            Bar(t.pct / 100.0, Accent.water)
            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
private fun TempsCard(s: HubState, meta: Meta) {
    if (s.tmp.none { it.on }) return
    SectionCard("Temperatures", Accent.climate) {
        Row(Modifier.fillMaxWidth()) {
            s.tmp.forEachIndexed { i, t ->
                Column(Modifier.weight(1f)) {
                    Text(meta.tempNames.getOrElse(i) { "Temp ${i + 1}" }, style = MaterialTheme.typography.labelSmall, color = Accent.muted)
                    Text(if (t.on) "${fmtDeg(t.c)}°" else "–", fontSize = 26.sp, fontWeight = FontWeight.SemiBold,
                        color = if (t.on) tempColor(i, t.c, MaterialTheme.colorScheme.onSurface) else Accent.muted)
                    if (t.on) Text("${fmtDeg(t.lo)}° / ${fmtDeg(t.hi)}°", style = MaterialTheme.typography.bodySmall, color = Accent.muted)
                }
            }
        }
    }
}

@Composable
private fun PumpsCard(s: HubState, meta: Meta) {
    if (s.pump.isEmpty()) return
    val hub = CaravanApp.instance.hub
    SectionCard("Pumps", Accent.water) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            s.pump.forEachIndexed { i, p ->
                val label = meta.pumpNames.getOrElse(i) { "Pump ${i + 1}" }
                Button(
                    onClick = { hub.send("pump", "i" to i, "on" to !p.on) },
                    enabled = !p.lockout,
                    modifier = Modifier.weight(1f),
                    colors = if (p.on) ButtonDefaults.buttonColors(containerColor = Accent.water)
                    else ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHighest,
                        contentColor = MaterialTheme.colorScheme.onSurface),
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(label, fontWeight = FontWeight.SemiBold)
                        Text(when { p.lockout -> "Locked out (dry)"; p.on -> "On"; else -> "Off" },
                            style = MaterialTheme.typography.labelSmall)
                    }
                }
            }
        }
    }
}

@Composable
fun Bar(fraction: Double, color: androidx.compose.ui.graphics.Color) {
    LinearProgressIndicator(
        progress = { fraction.toFloat().coerceIn(0f, 1f) },
        modifier = Modifier.fillMaxWidth().height(8.dp).clip(RoundedCornerShape(4.dp)),
        color = color,
        trackColor = MaterialTheme.colorScheme.surfaceContainerHighest,
        strokeCap = StrokeCap.Round,
        gapSize = 0.dp,
        drawStopIndicator = {},
    )
}

private fun socColor(soc: Double) = when { soc < 20 -> Accent.alarm; soc < 40 -> Accent.warn; else -> Accent.battery }
private fun fmt1(v: Double) = String.format(Locale.US, "%.1f", v)
// Temperatures are shown as whole degrees (rounded; never "-0").
private fun fmtDeg(v: Double) = Math.round(v).toInt().toString()
private fun fmt2(v: Double) = String.format(Locale.US, "%.2f", v)
private fun signed(v: Double) = (if (v > 0) "+" else "") + fmt1(v)
private fun ttgText(a: Double, ttg: Int): String = when {
    ttg < 0 -> if (a > 0.3) "Charging" else "Idle"
    a > 0 -> "Full in ${hm(ttg)}"
    else -> "${hm(ttg)} left at this load"
}
private fun hm(min: Int) = if (min >= 60) "${min / 60} h ${min % 60} min" else "$min min"
