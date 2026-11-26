package com.caravan.monitor.ui

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.material.icons.filled.LocalFireDepartment
import androidx.compose.material.icons.filled.Thermostat
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.style.TextAlign
import com.caravan.monitor.Conn
import com.caravan.monitor.Temp
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.DataAge
import com.caravan.monitor.Heater
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import kotlinx.coroutines.delay

/**
 * Climate tab: the three thermometers and the diesel heater (same card as the
 * touchscreen's Climate page). Heater commands go to the hub as
 * {"c":"heater", key: value}: on, mode ("temp"/"level"), temp, level.
 */
@Composable
fun ClimateContent(
    state: HubState?,
    meta: Meta,
    status: ConnStatus,
    onHeater: (key: String, value: Any) -> Unit,
    onOpenSetup: () -> Unit,
    /** Not live: a "No connection" banner and everything below greyed out. */
    age: DataAge = DataAge(),
) {
    val stale = !age.live && state != null
    Column(Modifier.fillMaxSize()) {
    if (stale) Box(Modifier.padding(start = 12.dp, end = 12.dp, top = 10.dp)) { NoConnectionBanner(age, status, onOpenSetup) }
    Column(
        Modifier.fillMaxWidth().weight(1f).greyedOut(stale).verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        val s = state
        ScreenHeader("Climate", if (s != null && s.wx.ok) "Wind ${s.wx.windKmh.toInt()} km/h, gusts ${s.wx.gustKmh.toInt()} km/h" else "",
            if (stale || s == null || status.conn != Conn.CONNECTED) null else if (status.cloud) "Live · via cloud" else "Live")
        if (s == null) {
            SectionCard("Climate", Accent.climate) { Hint(status.detail.ifBlank { "Not connected to the hub" }) }
            return@Column
        }
        SectionCard("Temperatures", Accent.climate, icon = Icons.Filled.Thermostat) {
            s.tmp.forEachIndexed { i, t -> TempRangeRow(meta.tempNames.getOrElse(i) { "Temp ${i + 1}" }, i, t) }
        }
        val h = s.htr
        if (h != null) HeaterCard(h, onHeater)
        else SectionCard("Diesel heater", Accent.heat) {
            Hint("No heater set up yet. Add it on the hub's web page: Setup > Diesel heater.")
            Spacer(Modifier.height(6.dp))
            OutlinedButton(onClick = onOpenSetup) { Text("Open Setup") }
        }
    }
    }
}

@Composable
fun HeaterCard(h: Heater, onHeater: (String, Any) -> Unit) {
    val ble = h.link == 1
    val rf = h.link == 2
    val lost = ble && !h.online
    val fault = h.hasStatus && h.err > 0
    val tone = when {
        lost -> Accent.muted
        fault -> Accent.alarm
        h.phase == 2 -> Accent.heat
        h.phase == 1 -> Accent.warn
        h.phase == 3 -> Accent.water
        else -> Accent.muted
    }
    val chip = if (lost) "Not connected" else if (fault) "Fault" else Heater.PHASES.getOrElse(h.phase) { "Off" }

    // A tap shows its set point straight away; the hub's status catches up.
    var pend by remember { mutableStateOf<Pair<String, Int>?>(null) }
    LaunchedEffect(pend) { if (pend != null) { delay(4000); pend = null } }
    val levelMode = h.mode == 1
    val shown = if (levelMode) pend?.takeIf { it.first == "level" }?.second ?: h.level
                else pend?.takeIf { it.first == "temp" }?.second ?: h.setC

    val running = h.running && !lost
    val dialColor = when { fault -> Accent.alarm; lost || !h.running -> Accent.muted; h.phase == 3 -> Accent.water; else -> Accent.heat }
    SectionCard("Diesel heater", Accent.heat, icon = Icons.Filled.LocalFireDepartment, trailing = { Pill(chip, tone) },
        // Warm glow behind the dial that breathes while the heater burns.
        inner = Modifier.cornerGlow(Accent.heat, if (running && h.phase != 3) 0.2f else 0f, atX = 0.5f, atY = 0.42f, radius = 160.dp,
            pulse = if (running && h.phase != 3) pulse(2600) else null),
    ) {
        val frac = if (levelMode) (shown - 1).toDouble() / (Heater.LEVELS - 1).coerceAtLeast(1)
                   else (shown - Heater.MIN_C).toDouble() / (Heater.MAX_C - Heater.MIN_C).coerceAtLeast(1)
        Box(Modifier.fillMaxWidth().height(206.dp).alpha(if (lost) 0.5f else 1f), contentAlignment = Alignment.Center) {
            HeaterDial(frac, dialColor, Modifier.size(206.dp))
            Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(150.dp).padding(top = 6.dp)) {
                Text(if (levelMode) "POWER LEVEL" else "SET TO", style = MaterialTheme.typography.labelSmall, color = Accent.muted,
                    fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
                GradText(if (levelMode) "$shown" else "$shown°", if (h.running && !fault) Accent.heat else Surfaces.text, 50.sp)
                Text(when {
                    lost -> "Looking for the heater over Bluetooth"
                    fault -> Heater.errorText(h.err)
                    rf -> if (h.running) "Start sent by remote" else "Stop sent by remote"
                    h.phase == 1 -> "Glow plug on, lighting"
                    h.phase == 3 -> "Fan runs on to cool it"
                    h.running -> if (levelMode) "Fixed power level" else "Holding the set temperature"
                    else -> "Off · tap power to start"
                }, style = MaterialTheme.typography.bodySmall, color = if (fault) Accent.alarm else Accent.muted,
                    textAlign = TextAlign.Center, maxLines = 2)
            }
            Box(Modifier.align(Alignment.BottomStart)) {
                StepButton(Icons.Filled.Remove, "Lower") {
                    val v = shown - 1
                    if (if (levelMode) v >= 1 else v >= Heater.MIN_C) { val k = if (levelMode) "level" else "temp"; pend = k to v; onHeater(k, v) }
                }
            }
            Box(Modifier.align(Alignment.BottomEnd)) {
                StepButton(Icons.Filled.Add, "Higher") {
                    val v = shown + 1
                    if (if (levelMode) v <= Heater.LEVELS else v <= Heater.MAX_C) { val k = if (levelMode) "level" else "temp"; pend = k to v; onHeater(k, v) }
                }
            }
        }
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().alpha(if (lost) 0.5f else 1f), horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.weight(1f).chip(radius = 14.dp).padding(4.dp)) {
                Segment("Thermostat", !levelMode, Modifier.weight(1f)) { pend = null; onHeater("mode", "temp") }
                Segment("Power level", levelMode, Modifier.weight(1f)) { pend = null; onHeater("mode", "level") }
            }
            val lit = h.running || h.phase == 3
            val pc = when (h.phase) { 3 -> Accent.water; 1 -> Accent.warn; else -> Accent.heat }
            Box(
                Modifier.size(width = 52.dp, height = 46.dp).clip(RoundedCornerShape(14.dp))
                    .then(if (lit) Modifier.background(Brush.linearGradient(listOf(light(pc, 0.35f), deep(pc, 0.1f)))) else Modifier.chip(radius = 14.dp))
                    .clickable { onHeater("on", !h.running) },
                contentAlignment = Alignment.Center,
            ) { Icon(Icons.Filled.PowerSettingsNew, if (h.running) "Stop the heater" else "Start the heater",
                    Modifier.size(24.dp), tint = if (lit) Color(0xFF2B1406) else Accent.muted) }
        }
        Spacer(Modifier.height(10.dp))
        if (rf) Hint("433 MHz remote: the heater doesn't report back")
        else Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val st = h.hasStatus
            Reading("Cabin", if (st && h.cabinC != null) "${fmtDeg(h.cabinC)}°" else "–",
                if (st && h.cabinC != null) tempColor(0, h.cabinC) else Accent.muted, Modifier.weight(1f))
            // The heat exchanger runs at 100-200 °C, so it's not on the room-temperature scale.
            Reading("Heater", if (st && h.bodyC != null) "${fmtDeg(h.bodyC)}°" else "–",
                if (!st || h.bodyC == null) Accent.muted else if (h.bodyC >= 60) Accent.heat else Surfaces.text, Modifier.weight(1f))
            Reading("Supply", if (st && h.supplyV != null) "${fmt1(h.supplyV)} V" else "–",
                if (!st || h.supplyV == null) Accent.muted else if (h.supplyV in 0.1..11.5) Accent.warn else Surfaces.text, Modifier.weight(1f))
        }
    }
}

/** 270° dial for the heater's set point (or power level); the arc sweeps to new values. */
@Composable
private fun HeaterDial(fraction: Double, color: Color, modifier: Modifier) {
    val f by animateFloatAsState(fraction.toFloat().coerceIn(0f, 1f), tween(700, easing = FastOutSlowInEasing), label = "dial")
    Canvas(modifier) {
        val sw = 13.dp.toPx()
        val tl = Offset(sw, sw)
        val arc = Size(size.width - 2 * sw, size.height - 2 * sw)
        drawArc(Grad.track, 135f, 270f, false, tl, arc, style = Stroke(sw, cap = StrokeCap.Round))
        val v = f
        val brush = Brush.linearGradient(listOf(light(color, 0.45f), color, deep(color, 0.05f)), Offset(0f, size.height), Offset(size.width, 0f))
        drawArc(brush, 135f, 270f * v, false, tl, arc, alpha = 0.25f, style = Stroke(sw * 1.9f, cap = StrokeCap.Round))
        drawArc(brush, 135f, 270f * v, false, tl, arc, style = Stroke(sw, cap = StrokeCap.Round))
        // knob at the end of the arc
        val a = (135f + 270f * v) * (PI / 180f).toFloat()
        val r = arc.width / 2
        val p = Offset(center.x + r * cos(a), center.y + r * sin(a))
        drawCircle(Color.White, sw * 0.62f, p)
        drawCircle(color, sw * 0.62f, p, style = Stroke(sw * 0.3f))
    }
}

/** One thermometer: name, reading, and a bar from today's low to high with a dot at now. */
@Composable
private fun TempRangeRow(name: String, i: Int, t: Temp) {
    Row(Modifier.fillMaxWidth().padding(vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(name, Modifier.width(66.dp), style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.SemiBold)
        if (!t.on) { Text("–", fontSize = 28.sp, fontWeight = FontWeight.Bold, color = Accent.muted); return@Row }
        GradText("${fmtTemp(i, t.c)}°", tempColor(i, t.c), 28.sp, Modifier.width(78.dp))
        Column(Modifier.weight(1f)) {
            RangeBar(i, t)
            Row(Modifier.fillMaxWidth().padding(top = 3.dp)) {
                Text("↓ ${fmtTemp(i, t.lo)}°", style = MaterialTheme.typography.labelSmall, color = Accent.muted.copy(alpha = 0.7f), modifier = Modifier.weight(1f))
                Text("↑ ${fmtTemp(i, t.hi)}°", style = MaterialTheme.typography.labelSmall, color = Accent.muted.copy(alpha = 0.7f))
            }
        }
    }
}

@Composable
private fun RangeBar(i: Int, t: Temp) {
    // Fixed scale per sensor so the bars compare day to day: air -5..40 °C, fridge -6..12 °C.
    val (lo, hi) = if (i == 2) -6.0 to 12.0 else -5.0 to 40.0
    fun x(v: Double) = ((v - lo) / (hi - lo)).toFloat().coerceIn(0f, 1f)
    val now by animateFloatAsState(x(t.c), tween(800, easing = FastOutSlowInEasing), label = "temp")
    val cLo = tempColor(i, t.lo); val cMid = tempColor(i, (t.lo + t.hi) / 2); val cHi = tempColor(i, t.hi); val cNow = tempColor(i, t.c)
    Canvas(Modifier.fillMaxWidth().height(14.dp)) {
        val h = 6.dp.toPx(); val y = (size.height - h) / 2
        val r = CornerRadius(h / 2, h / 2)
        drawRoundRect(Grad.track, Offset(0f, y), Size(size.width, h), r)
        val x0 = size.width * x(minOf(t.lo, t.c)); val x1 = size.width * x(maxOf(t.hi, t.c))
        if (x1 - x0 > 1f) drawRoundRect(Brush.horizontalGradient(listOf(cLo, cMid, cHi), x0, x1), Offset(x0, y), Size(x1 - x0, h), r)
        val p = Offset(size.width * now, size.height / 2)
        drawCircle(cNow.copy(alpha = 0.35f), 9.dp.toPx(), p)
        drawCircle(Color.White, 6.dp.toPx(), p)
        drawCircle(cNow, 3.dp.toPx(), p)
    }
}

@Composable
private fun StepButton(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, onClick: () -> Unit) {
    Box(
        Modifier.size(48.dp).clip(CircleShape).background(Color.Black.copy(alpha = 0.3f)).border(1.dp, Color.White.copy(alpha = 0.08f), CircleShape)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Icon(icon, label, tint = Surfaces.text) }
}

@Composable
private fun Segment(text: String, on: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier.height(38.dp).clip(RoundedCornerShape(11.dp))
            .then(if (on) Modifier.background(Brush.linearGradient(listOf(light(Accent.heat, 0.35f), Accent.heat))) else Modifier)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Text(text, color = if (on) Color(0xFF2B1406) else Accent.muted, style = MaterialTheme.typography.bodyMedium,
            fontWeight = if (on) FontWeight.Bold else FontWeight.SemiBold) }
}

@Composable
private fun Reading(label: String, value: String, color: Color, modifier: Modifier) {
    Column(modifier.chip(radius = 12.dp).padding(horizontal = 10.dp, vertical = 7.dp)) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = Accent.muted)
        Text(value, fontSize = 17.sp, fontWeight = FontWeight.Bold, color = color)
    }
}
