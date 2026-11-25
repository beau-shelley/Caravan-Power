package com.caravan.monitor.ui

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
        if (s == null) {
            SectionCard("Climate", Accent.climate) { Hint(status.detail.ifBlank { "Not connected to the hub" }) }
            return@Column
        }
        SectionCard("Temperatures", Accent.climate) {
            s.tmp.forEachIndexed { i, t ->
                Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(meta.tempNames.getOrElse(i) { "Temp ${i + 1}" }, color = Accent.muted, modifier = Modifier.weight(1f))
                    Text(if (t.on) "${fmtTemp(i, t.c)}°" else "–", fontSize = 26.sp, fontWeight = FontWeight.SemiBold,
                        color = if (t.on) tempColor(i, t.c) else Accent.muted)
                    Text(if (t.on) "  ↓${fmtTemp(i, t.lo)}° ↑${fmtTemp(i, t.hi)}°" else "", style = MaterialTheme.typography.bodySmall,
                        color = Accent.muted, modifier = Modifier.width(100.dp).padding(start = 6.dp))
                }
            }
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

    SectionCard("Diesel heater", Accent.heat, trailing = {
        Text(chip, style = MaterialTheme.typography.labelMedium, color = tone,
            modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(lerp(Surfaces.card, tone, 0.22f))
                .padding(horizontal = 8.dp, vertical = 3.dp))
    }) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            val lit = h.running || h.phase == 3
            val pc = when (h.phase) { 3 -> Accent.water; 1 -> Accent.warn; else -> Accent.heat }
            Box(
                Modifier.size(72.dp).alpha(if (lost) 0.5f else 1f).clip(CircleShape)
                    .background(if (lit) lerp(Surfaces.raised, pc, 0.25f) else Surfaces.raised)
                    .border(BorderStroke(2.dp, if (lit) pc else Surfaces.track), CircleShape)
                    .clickable { onHeater("on", !h.running) },
                contentAlignment = Alignment.Center,
            ) { Icon(Icons.Filled.PowerSettingsNew, if (h.running) "Stop the heater" else "Start the heater",
                    Modifier.size(36.dp), tint = if (lit) pc else Accent.muted) }
            Spacer(Modifier.width(14.dp))
            Column {
                Text(if (lost) "Not connected" else if (fault) "Fault" else if (h.running) "On" else "Off",
                    fontSize = 22.sp, fontWeight = FontWeight.SemiBold,
                    color = if (fault) Accent.alarm else if (lost) Accent.muted else Surfaces.text)
                Text(when {
                    lost -> "Looking for the heater over Bluetooth"
                    fault -> Heater.errorText(h.err)
                    rf -> if (h.running) "Start sent by remote" else "Stop sent by remote"
                    h.phase == 1 -> "Glow plug on, lighting"
                    h.phase == 3 -> "Fan runs on to cool it"
                    h.running -> if (levelMode) "Fixed power level" else "Holding the set temperature"
                    else -> "Tap to start"
                }, style = MaterialTheme.typography.bodySmall, color = if (fault) Accent.alarm else Accent.muted)
            }
        }
        Spacer(Modifier.height(12.dp))
        Row(Modifier.fillMaxWidth().alpha(if (lost) 0.5f else 1f), verticalAlignment = Alignment.CenterVertically) {
            StepButton(Icons.Filled.Remove, "Lower") {
                val v = shown - 1
                if (if (levelMode) v >= 1 else v >= Heater.MIN_C) { val k = if (levelMode) "level" else "temp"; pend = k to v; onHeater(k, v) }
            }
            Row(Modifier.weight(1f), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.Bottom) {
                Text(if (levelMode) "$shown" else "$shown°", fontSize = 40.sp, fontWeight = FontWeight.SemiBold,
                    color = if (levelMode) Accent.heat else tempColor(0, shown.toDouble()))
                Text(if (levelMode) " / ${Heater.LEVELS}" else " set", style = MaterialTheme.typography.bodySmall,
                    color = Accent.muted, modifier = Modifier.padding(bottom = 8.dp))
            }
            StepButton(Icons.Filled.Add, "Higher") {
                val v = shown + 1
                if (if (levelMode) v <= Heater.LEVELS else v <= Heater.MAX_C) { val k = if (levelMode) "level" else "temp"; pend = k to v; onHeater(k, v) }
            }
        }
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth().alpha(if (lost) 0.5f else 1f), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Segment("Thermostat", !levelMode, Modifier.weight(1f)) { pend = null; onHeater("mode", "temp") }
            Segment("Power level", levelMode, Modifier.weight(1f)) { pend = null; onHeater("mode", "level") }
        }
        Spacer(Modifier.height(12.dp))
        if (rf) Hint("433 MHz remote: the heater doesn't report back")
        else Row(Modifier.fillMaxWidth()) {
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

@Composable
private fun StepButton(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, onClick: () -> Unit) {
    Box(
        Modifier.size(width = 56.dp, height = 48.dp).clip(RoundedCornerShape(12.dp)).background(Surfaces.raised).clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Icon(icon, label, tint = Surfaces.text) }
}

@Composable
private fun Segment(text: String, on: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier.height(40.dp).clip(RoundedCornerShape(10.dp))
            .background(if (on) lerp(Surfaces.raised, Accent.heat, 0.2f) else Surfaces.raised)
            .border(1.dp, if (on) Accent.heat else Surfaces.track, RoundedCornerShape(10.dp))
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Text(text, color = if (on) Accent.heat else Accent.muted, style = MaterialTheme.typography.bodyMedium) }
}

@Composable
private fun Reading(label: String, value: String, color: Color, modifier: Modifier) {
    Column(modifier) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = Accent.muted)
        Text(value, fontSize = 20.sp, fontWeight = FontWeight.Medium, color = color)
    }
}
