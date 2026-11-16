package com.caravan.monitor.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import com.caravan.monitor.Rule

@Composable
fun HomeContent(
    state: HubState?,
    meta: Meta,
    rules: List<Rule>,
    status: ConnStatus,
    onAck: () -> Unit,
    onPump: (index: Int, on: Boolean) -> Unit,
    onOpenAlerts: () -> Unit,
    onOpenSetup: () -> Unit,
) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Caravan", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
        ConnectionStrip(status, onOpenSetup)
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
                    if (unacked.isNotEmpty()) Button(onClick = onAck) { Text("Acknowledge") }
                    FilledTonalButton(onClick = onOpenAlerts) { Text("All alerts") }
                }
            }
        }

        BatteryCard(s)
        if (s.bat.on && s.bat.acOn) MainsCard(s)
        SolarCard(s, meta)
        TanksCard(s, meta)
        TempsCard(s, meta)
        GasAndPumpsCard(s, meta, onPump)
        if (s.wx.ok) {
            SectionCard("Weather${if (s.wx.place.isNotBlank()) " · ${s.wx.place}" else ""}", Accent.climate) {
                Row(Modifier.fillMaxWidth()) {
                    Stat("Now", "${fmtDeg(s.wx.tempC)} °C", Modifier.weight(1f), valueColor = tempColor(1, s.wx.tempC))
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
                Hint(ttgText(b.a, b.ttgMin))
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
                    color = if (p.on) Surfaces.text else Accent.muted,
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
                        color = if (t.on) tempColor(i, t.c) else Accent.muted)
                    if (t.on) Hint("${fmtDeg(t.lo)}° / ${fmtDeg(t.hi)}°")
                }
            }
        }
    }
}

/** Gas bottle gauge on the left, the two pumps stacked on the right (like the touchscreen). */
@Composable
private fun GasAndPumpsCard(s: HubState, meta: Meta, onPump: (Int, Boolean) -> Unit) {
    SectionCard("Gas & pumps", Accent.gas) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.width(110.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                GasBottle(if (s.gas.on) s.gas.pct else null, Modifier.size(width = 64.dp, height = 96.dp))
                Spacer(Modifier.height(6.dp))
                Text(if (s.gas.on) "${s.gas.pct.toInt()}%" else "--", fontSize = 22.sp, fontWeight = FontWeight.Bold,
                    color = if (s.gas.on) gasColor(s.gas.pct) else Accent.muted)
                Hint("Gas bottle")
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                s.pump.forEachIndexed { i, p ->
                    val label = meta.pumpNames.getOrElse(i) { "Pump ${i + 1}" }
                    Button(
                        onClick = { onPump(i, !p.on) },
                        enabled = !p.lockout,
                        modifier = Modifier.fillMaxWidth().height(56.dp),
                        colors = if (p.on) ButtonDefaults.buttonColors(containerColor = Accent.water)
                        else ButtonDefaults.buttonColors(containerColor = Surfaces.track, contentColor = Surfaces.text),
                    ) {
                        Text(label, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                        Text(when { p.lockout -> "Locked (dry)"; p.on -> "On"; else -> "Off" },
                            style = MaterialTheme.typography.labelLarge)
                    }
                }
            }
        }
    }
}

/** A 9 kg bottle outline filled to [pct] (null = no reading). */
@Composable
fun GasBottle(pct: Double?, modifier: Modifier = Modifier) {
    Box(modifier) {
        Canvas(Modifier.fillMaxSize()) {
            val w = size.width; val h = size.height
            val stroke = 2.5f * density
            val collarW = w * 0.46f; val collarH = h * 0.16f
            val bodyTop = collarH + h * 0.04f
            val body = Size(w - stroke, h - bodyTop - stroke / 2)
            val bodyOff = Offset(stroke / 2, bodyTop)
            val r = CornerRadius(w * 0.28f, w * 0.28f)
            // fill
            if (pct != null) {
                val f = (pct / 100.0).coerceIn(0.0, 1.0).toFloat()
                val fillTop = bodyOff.y + body.height * (1 - f)
                clipRect(top = fillTop) {
                    drawRoundRect(gasColor(pct), bodyOff, body, r)
                }
            }
            // outline
            drawRoundRect(Accent.muted, bodyOff, body, r, style = Stroke(stroke))
            // collar / handle
            drawRoundRect(Accent.muted, Offset((w - collarW) / 2, 0f), Size(collarW, collarH),
                CornerRadius(4 * density, 4 * density), style = Stroke(stroke))
            // seam
            drawLine(Accent.muted.copy(alpha = 0.5f), Offset(bodyOff.x + stroke, bodyOff.y + body.height * 0.5f),
                Offset(bodyOff.x + body.width - stroke, bodyOff.y + body.height * 0.5f), strokeWidth = stroke * 0.6f)
        }
    }
}

private fun ttgText(a: Double, ttg: Int): String = when {
    ttg < 0 -> if (a > 0.3) "Charging" else "Idle"
    a > 0 -> "Full in ${hm(ttg)}"
    else -> "${hm(ttg)} left at this load"
}
private fun hm(min: Int) = if (min >= 60) "${min / 60} h ${min % 60} min" else "$min min"
