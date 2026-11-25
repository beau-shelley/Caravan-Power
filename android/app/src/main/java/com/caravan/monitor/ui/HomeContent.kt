package com.caravan.monitor.ui

import androidx.compose.foundation.Canvas
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.offset
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import kotlin.math.roundToInt
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
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
import androidx.compose.material3.Icon
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import com.caravan.monitor.Rule
import com.caravan.monitor.Secure
import com.caravan.monitor.fmtNum
import com.caravan.monitor.UpdateInfo

// Home is laid out to fit on one phone screen without scrolling (it still
// scrolls on very small screens): compact cards, two columns where it fits.
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
    update: UpdateInfo = UpdateInfo(),
    onUpdate: () -> Unit = {},
    /** Arm (true) or disarm (false) secure mode; the Android wrapper asks first. */
    onSecure: (Boolean) -> Unit = {},
    onArm: () -> Unit = {},
) {
    // Fills the screen on any phone: the main cards grow to use spare height, and it scrolls if it doesn't fit.
    BoxWithConstraints(Modifier.fillMaxSize()) {
    val fill = maxHeight - 20.dp
    FillColumn(
        fill,
        Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        ConnectionStrip(status, onOpenSetup)
        UpdateBanner(update, onUpdate)
        val s = state
        if (s == null) {
            Text("Caravan", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
            Text("Waiting for the hub…", color = Accent.muted)
            return@FillColumn
        }

        // Only shown while something needs attention; acknowledged alerts stay on the Alerts tab.
        val unacked = s.alerts.filter { !it.ack }
        if (unacked.isNotEmpty()) {
            // Tap the card for the Alerts tab; Acknowledge sits in the header to save a row.
            SectionCard(
                if (unacked.size > 2) "Alerts · ${unacked.size}" else "Alerts", sevColor(unacked.maxOf { it.sev }),
                Modifier.clickable(onClick = onOpenAlerts), compact = true,
                trailing = {
                    Button(onClick = onAck, modifier = Modifier.height(32.dp),
                        contentPadding = PaddingValues(horizontal = 14.dp)) { Text("Acknowledge", style = MaterialTheme.typography.labelLarge) }
                },
            ) {
                unacked.take(2).forEach { a -> AlertRow(a, rules.firstOrNull { it.id == a.id }?.name) }
            }
        }

        SecureCard(s.sec, onSecure, onArm)
        BatteryCard(s, Modifier.grow())
        Row(Modifier.fillMaxWidth().height(IntrinsicSize.Min).grow(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SolarCard(s, meta, Modifier.weight(1f).fillMaxHeight())
            TanksCard(s, meta, Modifier.weight(1f).fillMaxHeight())
        }
        TempsCard(s, meta, Modifier.grow())
        GasAndPumpsCard(s, meta, onPump, Modifier.grow())
    }
    }
}

/** Secure mode (anti-theft) in one slim row: lock / unlock icon on the left and, while off,
 *  the slide-to-arm bar beside it (the thumb has to go the whole width, so a stray tap can't arm).
 *  Sensitivity is set once in Setup and used every time. */
@Composable
private fun SecureCard(x: Secure, onSecure: (Boolean) -> Unit, onArm: () -> Unit) {
    val color = when (x.mode) {
        Secure.ARMING, Secure.ENTRY -> Accent.warn; Secure.ARMED -> Accent.secure; Secure.ALARM -> Accent.critical; else -> Accent.muted
    }
    val title = when (x.mode) {
        Secure.ARMING -> "Arming in ${x.armLeft} s"; Secure.ARMED -> "Secure mode armed"
        Secure.ALARM -> "THEFT ALARM"; Secure.ENTRY -> "Movement! Alarm in ${x.armLeft / 60}:${"%02d".format(x.armLeft % 60)}"
        else -> "Secure mode off"
    }
    val sub = when (x.mode) {
        Secure.ARMING -> "Time to leave the van"
        Secure.ARMED -> "Tilt ${fmt1(x.tilt)}° of ${fmtNum(x.tiltLim)}°, turned ${x.turn}° of ${x.turnLim}°"
        Secure.ALARM -> Secure.causeText(x.cause).replaceFirstChar { it.uppercase() }
        Secure.ENTRY -> "${Secure.causeText(x.cause).replaceFirstChar { it.uppercase() }} · disarm if it's you"
        else -> ""
    }
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Surfaces.glass),
        shape = RoundedCornerShape(16.dp),
        border = if (x.active) BorderStroke(1.5.dp, color) else null,
    ) {
        Row(Modifier.padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(44.dp).background(color.copy(alpha = 0.18f), RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
                Icon(when (x.mode) { Secure.ALARM, Secure.ENTRY -> CaravanIcons.Shield; Secure.OFF -> CaravanIcons.Unlock; else -> CaravanIcons.Lock },
                    null, Modifier.size(22.dp), tint = color)
            }
            Spacer(Modifier.width(8.dp))
            if (!x.active) {
                SlideToArm(enabled = true, onArm = onArm, modifier = Modifier.weight(1f))
            } else {
                Column(Modifier.weight(1f)) {
                    Text(title, fontWeight = FontWeight.Bold, color = color, style = MaterialTheme.typography.titleSmall)
                    Text(sub, style = MaterialTheme.typography.bodySmall, color = Accent.muted, maxLines = 1,
                        overflow = TextOverflow.Ellipsis)
                }
                Spacer(Modifier.width(8.dp))
                Button(
                    onClick = { onSecure(false) },
                    modifier = Modifier.height(40.dp),
                    contentPadding = PaddingValues(horizontal = 18.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Surfaces.track, contentColor = Surfaces.text),
                ) { Text("Disarm", fontWeight = FontWeight.SemiBold) }
            }
        }
    }
}

/** A track with a lock thumb. Dragging the thumb all the way to the right end arms;
 *  letting go anywhere short of the end springs it back. */
@Composable
fun SlideToArm(enabled: Boolean, onArm: () -> Unit, modifier: Modifier = Modifier) {
    val thumb = 44.dp
    val density = LocalDensity.current
    val thumbPx = with(density) { thumb.toPx() }
    var widthPx by remember { mutableStateOf(0f) }
    var drag by remember { mutableStateOf(0f) }
    var dragging by remember { mutableStateOf(false) }
    val maxPx = (widthPx - thumbPx).coerceAtLeast(1f)
    val shown by animateFloatAsState(if (dragging) drag else 0f, label = "slide")
    val frac = (shown / maxPx).coerceIn(0f, 1f)
    Box(
        modifier
            .height(thumb)
            .onSizeChanged { widthPx = it.width.toFloat() }
            .background(Surfaces.track, RoundedCornerShape(50))
            .pointerInput(enabled, maxPx) {
                if (!enabled) return@pointerInput
                detectHorizontalDragGestures(
                    onDragStart = { dragging = true; drag = 0f },
                    onDragEnd = {
                        if (drag >= maxPx * 0.97f) onArm()
                        dragging = false; drag = 0f
                    },
                    onDragCancel = { dragging = false; drag = 0f },
                    onHorizontalDrag = { change, dx -> change.consume(); drag = (drag + dx).coerceIn(0f, maxPx) },
                )
            },
        contentAlignment = Alignment.CenterStart,
    ) {
        // Green fill that follows the thumb.
        Box(Modifier.width(with(density) { (shown + thumbPx).toDp() }).fillMaxHeight()
            .background(Accent.secure.copy(alpha = 0.25f + 0.35f * frac), RoundedCornerShape(50)))
        Text("Slide to arm  \u203A\u203A",
            Modifier.fillMaxWidth().padding(start = thumb), textAlign = TextAlign.Center,
            color = Accent.muted.copy(alpha = 1f - frac), fontWeight = FontWeight.SemiBold,
            style = MaterialTheme.typography.labelLarge)
        Box(
            Modifier.offset { IntOffset(shown.roundToInt(), 0) }.size(thumb)
                .background(if (enabled) Accent.secure else Accent.muted, RoundedCornerShape(50)),
            contentAlignment = Alignment.Center,
        ) { Icon(CaravanIcons.Lock, null, Modifier.size(22.dp), tint = Surfaces.card) }
    }
}

@Composable
private fun BatteryCard(s: HubState, modifier: Modifier = Modifier) {
    val b = s.bat
    SectionCard("Battery", Accent.battery, modifier, compact = true) {
        if (!b.on) { Text("Offline", color = Accent.muted); return@SectionCard }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("${b.soc.toInt()}%", fontSize = 34.sp, fontWeight = FontWeight.Bold, color = socColor(b.soc))
            Spacer(Modifier.width(12.dp))
            Column {
                Text("${fmt2(b.v)} V  ·  ${signed(b.a)} A", style = MaterialTheme.typography.bodyLarge)
                Hint(ttgText(b.a, b.ttgMin))
            }
        }
        Spacer(Modifier.height(6.dp))
        Bar(b.soc / 100.0, socColor(b.soc))
        Spacer(Modifier.height(6.dp))
        Row(Modifier.fillMaxWidth()) {
            // Mains is only shown while the hub detects bulk mains charging (worked out, not measured).
            if (b.acOn) MiniStat("Mains", "${b.acA.toInt()} A", Modifier.weight(1f), Accent.mains)
            // Total charge coming in (all solar, BMPRO and mains), as on the display.
            MiniStat("Charging", "${fmt1(b.chargeA)} A", Modifier.weight(1f))
            MiniStat("Load", "${fmt1(b.loadA)} A", Modifier.weight(1f))
        }
    }
}

@Composable
private fun SolarCard(s: HubState, meta: Meta, modifier: Modifier) {
    val total = s.sol.filter { it.on }.sumOf { it.w }
    SectionCard("Solar", Accent.solar, modifier, compact = true) {
        Text("${total.toInt()} W", fontSize = 24.sp, fontWeight = FontWeight.Bold, color = Accent.solar)
        // Only sources making power get a row (a panel at 0 W is hidden, like the touchscreen).
        s.sol.forEachIndexed { i, p ->
            if (!p.on || p.w < 1) return@forEachIndexed
            Row(Modifier.fillMaxWidth().padding(top = 3.dp)) {
                Text(meta.solarNames.getOrElse(i) { "Solar ${i + 1}" }, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                Text("${p.w.toInt()} W", style = MaterialTheme.typography.bodyMedium)
            }
        }
        Hint("${fmt1(s.sol.filter { it.on }.sumOf { it.yieldKwh })} kWh today")
    }
}

@Composable
private fun TanksCard(s: HubState, meta: Meta, modifier: Modifier) {
    SectionCard("Water", Accent.water, modifier, icon = CaravanIcons.Drop, compact = true) {
        if (s.tank.none { it.on }) { Text("No tanks", color = Accent.muted); return@SectionCard }
        s.tank.forEachIndexed { i, t ->
            if (!t.on) return@forEachIndexed
            Row(Modifier.fillMaxWidth()) {
                Text(meta.tankNames.getOrElse(i) { "Tank ${i + 1}" }, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                Text("${t.pct.toInt()}%", style = MaterialTheme.typography.bodyMedium)
            }
            Spacer(Modifier.height(2.dp))
            Bar(t.pct / 100.0, Accent.water)
            Spacer(Modifier.height(4.dp))
        }
    }
}

@Composable
private fun TempsCard(s: HubState, meta: Meta, modifier: Modifier = Modifier) {
    // Called Weather: the temperatures plus today's wind, with the forecast's place by the heading.
    SectionCard("Weather", Accent.climate, modifier, compact = true, trailing = if (s.wx.ok && s.wx.place.isNotBlank()) ({
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(CaravanIcons.LocationArrow, null, Modifier.size(12.dp), tint = Accent.muted)
            Spacer(Modifier.width(4.dp))
            Text(s.wx.place, style = MaterialTheme.typography.labelMedium, color = Accent.muted, maxLines = 1,
                overflow = TextOverflow.Ellipsis)
        }
    }) else null) {
        Row(Modifier.fillMaxWidth()) {
            s.tmp.forEachIndexed { i, t ->
                Column(Modifier.weight(1f)) {
                    Text(meta.tempNames.getOrElse(i) { "Temp ${i + 1}" }, style = MaterialTheme.typography.labelSmall, color = Accent.muted)
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(if (t.on) "${fmtTemp(i, t.c)}°" else "–", fontSize = 24.sp, fontWeight = FontWeight.SemiBold,
                            color = if (t.on) tempColor(i, t.c) else Accent.muted)
                        if (t.on) Text("  ${fmtTemp(i, t.lo)}/${fmtTemp(i, t.hi)}°", style = MaterialTheme.typography.bodySmall,
                            color = Accent.muted, modifier = Modifier.padding(bottom = 4.dp))
                    }
                }
            }
        }
        // Heater set point, only while the heater runs (like the touchscreen).
        val h = s.htr
        if (h != null && h.hasStatus && h.running) {
            Row(Modifier.fillMaxWidth().padding(top = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(8.dp).background(Accent.heat, RoundedCornerShape(50)))
                Spacer(Modifier.width(8.dp))
                Text(if (h.mode == 1) "Heater level" else "Heater set", Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyMedium, color = Accent.muted)
                Text(if (h.mode == 1) "${h.level}" else "${h.setC}°", style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold, color = if (h.mode == 1) Surfaces.text else tempColor(0, h.setC.toDouble()))
            }
        }
        // Today's forecast, folded in to save a card.
        if (s.wx.ok) Hint("Wind ${s.wx.windKmh.toInt()} km/h, gusts ${s.wx.gustKmh.toInt()} km/h")
    }
}

/** Label over value, small enough for four across. */
@Composable
private fun MiniStat(label: String, value: String, modifier: Modifier, color: androidx.compose.ui.graphics.Color = Surfaces.text) {
    Column(modifier) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = Accent.muted)
        Text(value, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium, color = color)
    }
}

/** Gas bottle gauge on the left, the two pumps stacked on the right (like the touchscreen). */
@Composable
private fun GasAndPumpsCard(s: HubState, meta: Meta, onPump: (Int, Boolean) -> Unit, modifier: Modifier = Modifier) {
    SectionCard("Gas & pumps", Accent.gas, modifier, compact = true) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.width(118.dp), verticalAlignment = Alignment.CenterVertically) {
                GasBottle(if (s.gas.on) s.gas.pct else null, Modifier.size(width = 40.dp, height = 62.dp))
                Spacer(Modifier.width(10.dp))
                Column {
                    Text(if (s.gas.on) "${s.gas.pct.toInt()}%" else "--", fontSize = 22.sp, fontWeight = FontWeight.Bold,
                        color = if (s.gas.on) gasColor(s.gas.pct) else Accent.muted)
                    Hint("Gas")
                }
            }
            Spacer(Modifier.width(8.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                s.pump.forEachIndexed { i, p ->
                    val label = meta.pumpNames.getOrElse(i) { "Pump ${i + 1}" }
                    Button(
                        onClick = { onPump(i, !p.on) },
                        enabled = !p.lockout,
                        modifier = Modifier.fillMaxWidth().height(42.dp),
                        contentPadding = PaddingValues(horizontal = 16.dp),
                        colors = if (p.on) ButtonDefaults.buttonColors(containerColor = Accent.water)
                        else ButtonDefaults.buttonColors(containerColor = Surfaces.track, contentColor = Surfaces.text),
                    ) {
                        Icon(CaravanIcons.Tap, null, Modifier.size(20.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(label, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                        Text(when { p.lockout -> "Dry"; p.on -> "On"; else -> "Off" },
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
