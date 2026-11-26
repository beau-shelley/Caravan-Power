package com.caravan.monitor.ui

import androidx.compose.foundation.Canvas
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryChargingFull
import androidx.compose.material.icons.filled.Thermostat
import androidx.compose.material.icons.filled.WbSunny
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.foundation.border
import androidx.compose.ui.draw.clip
import com.caravan.monitor.Conn
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
import com.caravan.monitor.DataAge
import com.caravan.monitor.HubState
import com.caravan.monitor.Meta
import com.caravan.monitor.Outlook
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
    /** Not live: the banner says "No connection" and every value below is greyed out. */
    age: DataAge = DataAge(),
    /** Battery time and gas days worked out on the phone (empty while not live). */
    outlook: Outlook = Outlook(),
) {
    val stale = !age.live && state != null
    Column(Modifier.fillMaxSize()) {
    if (stale) {
        Column(Modifier.padding(start = 12.dp, end = 12.dp, top = 10.dp)) {
            NoConnectionBanner(age, status, onOpenSetup)
            UpdateBanner(update, onUpdate)
        }
    }
    // Fills the screen on any phone: the main cards grow to use spare height, and it scrolls if it doesn't fit.
    BoxWithConstraints(Modifier.fillMaxWidth().weight(1f)) {
    val fill = maxHeight - 20.dp
    FillColumn(
        fill,
        Modifier.fillMaxWidth().greyedOut(stale).verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        val s = state
        ScreenHeader("Caravan", listOfNotNull(s?.wx?.takeIf { it.ok && it.place.isNotBlank() }?.place, todayText()).joinToString(" · "),
            if (stale || s == null || status.conn != Conn.CONNECTED) null else if (status.cloud) "Live · via cloud" else "Live")
        if (!stale) {
            // The header's pill says live / via cloud; the strip is only for trouble (searching, login failed...).
            if (status.conn != Conn.CONNECTED) ConnectionStrip(status, onOpenSetup)
            UpdateBanner(update, onUpdate)
        }
        if (s == null) {
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
        BatteryCard(s, outlook, Modifier.grow())
        Row(Modifier.fillMaxWidth().height(IntrinsicSize.Min).grow(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SolarCard(s, meta, Modifier.weight(1f).fillMaxHeight())
            TanksCard(s, meta, Modifier.weight(1f).fillMaxHeight())
        }
        TempsCard(s, meta, Modifier.grow())
        GasAndPumpsCard(s, meta, outlook, onPump, Modifier.grow())
    }
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
    if (!x.active) {
        // Off: just the slide bar, in a slim rounded card.
        GlassCard(radius = 30) { Box(Modifier.padding(5.dp)) { SlideToArm(enabled = true, onArm = onArm, modifier = Modifier.fillMaxWidth()) } }
        return
    }
    GlassCard(
        Modifier.border(1.5.dp, color, RoundedCornerShape(22.dp)),
        inner = Modifier.cornerGlow(color, 0.22f, atX = 0f, atY = 0.5f, radius = 120.dp,
            pulse = if (x.mode == Secure.ALARM || x.mode == Secure.ENTRY) pulse(900) else null),
    ) {
        Row(Modifier.padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(44.dp).clip(RoundedCornerShape(14.dp)).background(Brush.verticalGradient(listOf(color.copy(alpha = 0.3f), color.copy(alpha = 0.12f)))),
                contentAlignment = Alignment.Center) {
                Icon(when (x.mode) { Secure.ALARM, Secure.ENTRY -> CaravanIcons.Shield; else -> CaravanIcons.Lock },
                    null, Modifier.size(22.dp), tint = color)
            }
            Spacer(Modifier.width(10.dp))
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
                colors = ButtonDefaults.buttonColors(containerColor = Grad.track, contentColor = Surfaces.text),
            ) { Text("Disarm", fontWeight = FontWeight.SemiBold) }
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
            .clip(RoundedCornerShape(50))
            .background(Grad.track)
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
        // Green gradient that follows the thumb and fades out to the right.
        Box(Modifier.width(with(density) { (shown + thumbPx * 2.4f).toDp() }).fillMaxHeight()
            .background(Brush.horizontalGradient(listOf(Accent.secure.copy(alpha = 0.35f + 0.35f * frac), Color.Transparent)), RoundedCornerShape(50)))
        Row(Modifier.fillMaxWidth().padding(start = thumb).graphicsLayer { alpha = 1f - frac },
            horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
            Text("Slide to arm secure mode", color = Accent.muted, fontWeight = FontWeight.SemiBold,
                style = MaterialTheme.typography.labelLarge, maxLines = 1)
            Spacer(Modifier.width(6.dp))
            if (enabled) ShimmerChevrons(Accent.secure)
        }
        Box(
            Modifier.offset { IntOffset(shown.roundToInt(), 0) }.size(thumb)
                .clip(RoundedCornerShape(50))
                .background(if (enabled) Brush.linearGradient(listOf(light(Accent.secure, 0.35f), deep(Accent.secure, 0.2f)))
                            else Brush.linearGradient(listOf(Accent.muted, Accent.muted))),
            contentAlignment = Alignment.Center,
        ) { Icon(CaravanIcons.Lock, null, Modifier.size(20.dp), tint = Surfaces.onLime) }
    }
}

@Composable
private fun BatteryCard(s: HubState, outlook: Outlook, modifier: Modifier = Modifier) {
    val b = s.bat
    val charging = b.on && b.a > 0.3
    val using = b.on && b.a < -0.3
    val soc = socColor(b.soc)
    SectionCard("Battery", Accent.battery, modifier, icon = Icons.Filled.BatteryChargingFull, compact = true,
        inner = Modifier.cornerGlow(soc, 0.14f, atX = 0.85f, atY = 0f, radius = 150.dp),
        trailing = if (b.on) ({
            Pill(when { charging -> "Charging"; using -> "Using"; else -> "Resting" },
                when { charging -> Accent.battery; using -> Accent.warn; else -> Accent.muted })
        }) else null,
    ) {
        if (!b.on) { Text("Offline", color = Accent.muted); return@SectionCard }
        Row(verticalAlignment = Alignment.CenterVertically) {
            RingGauge(b.soc / 100.0, soc, Modifier.size(90.dp), stroke = 9.dp, spark = charging) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    GradText("${Math.round(b.soc)}%", soc, 24.sp)
                    Text("${fmt2(b.v)} V", style = MaterialTheme.typography.labelSmall, color = Accent.muted)
                }
            }
            Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) {
                // Net = everything coming in minus everything going out, as measured at the battery.
                Text(if (b.a < 0) "Net from battery" else "Net into battery", style = MaterialTheme.typography.labelMedium, color = Accent.muted)
                GradValue(signed(b.a), "A", if (charging || using) netColor(b.a) else Surfaces.text, 30.sp)
                Spacer(Modifier.height(4.dp))
                Text(outlook.battery ?: ttgText(b.a, b.ttgMin), style = MaterialTheme.typography.bodyMedium, color = Accent.muted)
            }
        }
        Spacer(Modifier.height(8.dp))
        ChipRow {
            // Mains is only shown while the hub detects bulk mains charging (worked out, not measured).
            if (b.acOn) StatChip("Mains", "${b.acA.toInt()} A", Modifier.weight(1f), Accent.mains, stacked = true)
            // Total charge coming in (all solar, BMPRO and mains), as on the display.
            StatChip("Charging", "${fmt1(b.chargeA)} A", Modifier.weight(1f), stacked = b.acOn)
            StatChip("Load", "${fmt1(b.loadA)} A", Modifier.weight(1f), stacked = b.acOn)
        }
    }
}

/** Label on the left, value on the right, in a sunk-in chip. */
@Composable
private fun StatChip(label: String, value: String, modifier: Modifier, color: Color = Surfaces.text, stacked: Boolean = false) {
    // Three across (mains charging) doesn't leave room side by side: label over value.
    if (stacked) {
        Column(modifier.chip(radius = 12.dp).padding(horizontal = 10.dp, vertical = 5.dp)) {
            Text(label, style = MaterialTheme.typography.labelSmall, color = Accent.muted, maxLines = 1)
            Text(value, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold, color = color, maxLines = 1)
        }
        return
    }
    Row(modifier.chip(radius = 12.dp).padding(horizontal = 10.dp, vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = Accent.muted, modifier = Modifier.weight(1f), maxLines = 1)
        Text(value, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.Bold, color = color, maxLines = 1)
    }
}

@Composable
private fun SolarCard(s: HubState, meta: Meta, modifier: Modifier) {
    // Solar in amps (charge current into the battery), as on the touchscreen.
    val total = s.sol.filter { it.on }.sumOf { it.a }
    val watts = s.sol.filter { it.on }.sumOf { it.w }
    val making = total >= 0.3
    SectionCard("Solar", Accent.solar, modifier, icon = Icons.Filled.WbSunny, compact = true,
        // The sun's glow breathes while the panels are making power.
        inner = Modifier.cornerGlow(Accent.solar, if (making) 0.22f else 0.06f, radius = 90.dp, pulse = if (making) pulse(3200) else null)) {
        GradValue(fmt1(total), "A", if (making) Accent.solar else Accent.muted, 28.sp)
        Hint("${watts.toInt()} W · ${fmt1(s.sol.filter { it.on }.sumOf { it.yieldKwh })} kWh today")
        // Only sources making power get a row (a panel at 0 A is hidden, like the touchscreen).
        s.sol.forEachIndexed { i, p ->
            if (!p.on || (p.a < 0.3 && p.w < 5)) return@forEachIndexed
            Column(Modifier.padding(top = 5.dp)) {
                Row(Modifier.fillMaxWidth()) {
                    Text(meta.solarNames.getOrElse(i) { "Solar ${i + 1}" }, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium, color = Accent.muted)
                    Text("${fmt1(p.a)} A", style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Bold)
                }
                Spacer(Modifier.height(3.dp))
                // Each source's share of the solar coming in.
                GradBar(if (total > 0) p.a / total else 0.0, Accent.solar, height = 5.dp)
            }
        }
    }
}

@Composable
private fun TanksCard(s: HubState, meta: Meta, modifier: Modifier) {
    SectionCard("Water", Accent.water, modifier, icon = CaravanIcons.Drop, compact = true) {
        if (s.tank.none { it.on }) { Text("No tanks", color = Accent.muted); return@SectionCard }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            s.tank.forEachIndexed { i, t ->
                if (!t.on) return@forEachIndexed
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(contentAlignment = Alignment.Center) {
                        WaveTank(t.pct, if (t.pct < 15) Accent.warn else Accent.water, Modifier.size(width = if (s.tank.count { it.on } > 2) 42.dp else 50.dp, height = 64.dp))
                        Text("${t.pct.toInt()}%", fontWeight = FontWeight.ExtraBold, fontSize = 15.sp, color = Color.White)
                    }
                    Spacer(Modifier.height(5.dp))
                    Text(meta.tankNames.getOrElse(i) { "Tank ${i + 1}" }, style = MaterialTheme.typography.labelMedium, color = Accent.muted, maxLines = 1)
                }
            }
        }
    }
}

@Composable
private fun TempsCard(s: HubState, meta: Meta, modifier: Modifier = Modifier) {
    // The three thermometers as tiles, with today's wind in the heading.
    SectionCard("Temperatures", Accent.climate, modifier, icon = Icons.Filled.Thermostat, compact = true,
        trailing = if (s.wx.ok) ({ Text("Wind ${s.wx.windKmh.toInt()} km/h", style = MaterialTheme.typography.labelMedium, color = Accent.muted) }) else null) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            s.tmp.forEachIndexed { i, t ->
                val col = if (t.on) tempColor(i, t.c) else Accent.muted
                Column(Modifier.weight(1f).chip(if (t.on && col != Surfaces.text) col else null).padding(horizontal = 10.dp, vertical = 5.dp)) {
                    Text(meta.tempNames.getOrElse(i) { "Temp ${i + 1}" }, style = MaterialTheme.typography.labelMedium, color = Accent.muted, maxLines = 1)
                    if (t.on) GradText("${fmtTemp(i, t.c)}°", col, 21.sp)
                    else Text("–", fontSize = 21.sp, fontWeight = FontWeight.Bold, color = Accent.muted)
                    if (t.on) Text("${fmtTemp(i, t.lo)}° – ${fmtTemp(i, t.hi)}°", style = MaterialTheme.typography.labelSmall,
                        color = Accent.muted.copy(alpha = 0.7f), maxLines = 1)
                }
            }
        }
        // Heater set point, only while the heater runs (like the touchscreen).
        val h = s.htr
        if (h != null && h.hasStatus && h.running) {
            Row(Modifier.fillMaxWidth().padding(top = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(8.dp).background(Accent.heat, RoundedCornerShape(50)))
                Spacer(Modifier.width(8.dp))
                Text(if (h.mode == 1) "Heater level" else "Heater set", Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyMedium, color = Accent.muted)
                Text(if (h.mode == 1) "${h.level}" else "${h.setC}°", style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold, color = if (h.mode == 1) Surfaces.text else tempColor(0, h.setC.toDouble()))
            }
        }
    }
}

/** Net battery current: green while it's going up, amber while it's going down. */
fun netColor(a: Double) = when { a > 0.3 -> Accent.battery; a < -0.3 -> Accent.warn; else -> Surfaces.text }

/** Gas bottle gauge on the left, the two pumps stacked on the right (like the touchscreen). */
@Composable
private fun GasAndPumpsCard(s: HubState, meta: Meta, outlook: Outlook, onPump: (Int, Boolean) -> Unit, modifier: Modifier = Modifier) {
    GlassCard(modifier) {
        Row(Modifier.fillMaxHeight().padding(horizontal = 12.dp, vertical = 9.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.width(132.dp), verticalAlignment = Alignment.CenterVertically) {
                GasBottle(if (s.gas.on) s.gas.pct else null, Modifier.size(width = 32.dp, height = 50.dp))
                Spacer(Modifier.width(10.dp))
                Column {
                    if (s.gas.on) GradText("${s.gas.pct.toInt()}%", gasColor(s.gas.pct), 23.sp)
                    else Text("--", fontSize = 23.sp, fontWeight = FontWeight.Bold, color = Accent.muted)
                    // Days left at the recent rate (worked out on the phone over the last week).
                    Text(if (s.gas.on && outlook.gas != null) outlook.gas else "Gas",
                        style = MaterialTheme.typography.labelSmall, color = Accent.muted, maxLines = 1)
                }
            }
            Spacer(Modifier.width(8.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                s.pump.forEachIndexed { i, p -> PumpButton(meta.pumpNames.getOrElse(i) { "Pump ${i + 1}" }, p.on, p.lockout) { onPump(i, !p.on) } }
            }
        }
    }
}

/** Pump toggle: a blue gradient with a glow while on, a sunk-in chip while off, faded when locked out (dry). */
@Composable
private fun PumpButton(label: String, on: Boolean, lockout: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(13.dp)
    val ink = if (on) Color(0xFF04263B) else Surfaces.text
    Row(
        Modifier.fillMaxWidth().height(36.dp).graphicsLayer { alpha = if (lockout) 0.5f else 1f }
            .then(if (on) Modifier.clip(shape).background(Brush.linearGradient(listOf(light(Accent.water, 0.35f), deep(Accent.water, 0.05f))))
                  else Modifier.chip(radius = 13.dp))
            .clickable(enabled = !lockout, onClick = onClick)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(CaravanIcons.Tap, null, Modifier.size(17.dp), tint = ink)
        Spacer(Modifier.width(8.dp))
        Text(label, fontWeight = FontWeight.Bold, color = ink, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f), maxLines = 1)
        Text(when { lockout -> "Dry"; on -> "On"; else -> "Off" }, style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.Bold, color = if (on) ink else Accent.muted)
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
            if (pct == null) drawRoundRect(Grad.track, bodyOff, body, r)
            if (pct != null) {
                val f = (pct / 100.0).coerceIn(0.0, 1.0).toFloat()
                val fillTop = bodyOff.y + body.height * (1 - f)
                drawRoundRect(Grad.track, bodyOff, body, r)
                clipRect(top = fillTop) {
                    drawRoundRect(Brush.verticalGradient(listOf(light(gasColor(pct), 0.3f), deep(gasColor(pct), 0.1f)), fillTop, bodyOff.y + body.height), bodyOff, body, r)
                }
            }
            // outline
            drawRoundRect(Color.White.copy(alpha = 0.18f), bodyOff, body, r, style = Stroke(stroke))
            // collar / handle
            drawRoundRect(Accent.muted, Offset((w - collarW) / 2, 0f), Size(collarW, collarH),
                CornerRadius(4 * density, 4 * density), style = Stroke(stroke))
            // seam
            drawLine(Color.White.copy(alpha = 0.12f), Offset(bodyOff.x + stroke, bodyOff.y + body.height * 0.5f),
                Offset(bodyOff.x + body.width - stroke, bodyOff.y + body.height * 0.5f), strokeWidth = stroke * 0.6f)
        }
    }
}

/** "Thu 26 Nov" for the Home header. */
private fun todayText(): String = java.time.LocalDate.now().format(java.time.format.DateTimeFormatter.ofPattern("EEE d MMM", java.util.Locale.ENGLISH))

private fun ttgText(a: Double, ttg: Int): String = when {
    ttg < 0 -> if (a > 0.3) "Charging" else "Idle"
    a > 0 -> "Full in ${hm(ttg)}"
    else -> "${hm(ttg)} left at this load"
}
private fun hm(min: Int) = if (min >= 60) "${min / 60} h ${min % 60} min" else "$min min"
