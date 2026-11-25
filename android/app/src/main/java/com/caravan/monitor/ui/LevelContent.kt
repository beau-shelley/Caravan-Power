package com.caravan.monitor.ui

// Level page: the same bubble level and "raise wheel X mm" advice as the
// touchscreen's Level page, from the angles the hub sends in its state.

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caravan.monitor.Conn
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.HubState
import com.caravan.monitor.LevelData
import kotlin.math.abs
import kotlin.math.sqrt

private const val DEG_FULL = 3.0  // degrees at the outer ring

@Composable
fun LevelContent(
    state: HubState?,
    status: ConnStatus,
    onZero: () -> Unit,
    onFront: () -> Unit,
    onDims: (track: Int, jockey: Int) -> Unit,
    /** Offered once the van has been level for a moment: set the weather location to here. */
    onSetLocationHere: (() -> Unit)? = null,
) {
    val l = state?.lvl ?: LevelData()
    val live = status.conn == Conn.CONNECTED && state != null
    var ask by remember { mutableStateOf<String?>(null) }  // "zero" | "front"

    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Level", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
        ConnectionStrip(status)

        SectionCard("Bubble level", Accent.lime) {
            if (!live || !l.online) {
                Text(if (!live) "Not connected to the hub." else "No tilt sensor reading from the hub.",
                    color = Accent.warn)
                Hint("The level comes from the tilt sensor (MPU-6050) in the hub box.")
            }
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                Bubble(l, live && l.online, Modifier.widthIn(max = 320.dp).fillMaxWidth().aspectRatio(1f))
            }
            if (live && l.online) {
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Stat("Side to side", if (l.rollOk) "✓ ${fmt1(abs(l.roll))}°" else "${fmt1(abs(l.roll))}° ${if (l.roll < 0) "left" else "right"} low",
                        Modifier.weight(1f), valueColor = if (l.rollOk) Accent.battery else Surfaces.text)
                    Stat("Front to back", if (l.pitchOk) "✓ ${fmt1(abs(l.pitch))}°" else "${fmt1(abs(l.pitch))}° front ${if (l.pitch > 0) "high" else "low"}",
                        Modifier.weight(1f), valueColor = if (l.pitchOk) Accent.battery else Surfaces.text)
                }
            }
        }

        if (live && l.online) SectionCard("What to do", if (l.level) Accent.battery else Accent.warn) {
            Text(if (l.rollOk) "✓ Wheels level" else "${if (l.raiseLeft) "Left" else "Right"} wheel: up ${l.wheelMm} mm",
                fontSize = 22.sp, fontWeight = FontWeight.Bold, color = if (l.rollOk) Accent.battery else Surfaces.text)
            Spacer(Modifier.height(4.dp))
            Text(if (l.pitchOk) "✓ Jockey level" else "Jockey: ${if (l.jockeyAdj > 0) "up" else "down"} ${abs(l.jockeyAdj)} mm",
                fontSize = 22.sp, fontWeight = FontWeight.Bold, color = if (l.pitchOk) Accent.battery else Surfaces.text)
            Spacer(Modifier.height(8.dp))
            Hint(when {
                l.level -> "Level. Chock the wheels and lower the stabilisers."
                !l.zeroed -> "Not calibrated. Park on level ground and press Set level."
                !l.frontLearned -> "Calibrated. Optional: Learn front if the sensor isn't square to the van. Do the wheels first, then the jockey."
                else -> "Do the wheels first, then the jockey."
            })
            if (l.level && onSetLocationHere != null) {
                Spacer(Modifier.height(8.dp))
                OutlinedButton(onClick = onSetLocationHere) { Text("Set the weather location to here") }
            }
        }

        SectionCard("Calibrate", Accent.muted) {
            when (ask) {
                "zero" -> Confirm("Is the van exactly level right now? Check with a spirit level on the floor, then keep still for 2 seconds.",
                    "Set level", { onZero(); ask = null }, { ask = null })
                "front" -> Confirm("Wind the jockey wheel UP so the front is 2-3 cm higher than level, then press Learn front. (Only needed once, after Set level.)",
                    "Learn front", { onFront(); ask = null }, { ask = null })
                else -> Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = { ask = "zero" }, enabled = live) { Text("Set level") }
                    OutlinedButton(onClick = { ask = "front" }, enabled = live && l.zeroed) { Text("Learn front") }
                }
            }
        }

        SectionCard("Van size", Accent.muted) {
            Hint("Used to turn the angles into millimetres.")
            var track by remember(l.trackMm) { mutableStateOf(l.trackMm.toString()) }
            var jockey by remember(l.jockeyMm) { mutableStateOf(l.jockeyMm.toString()) }
            val t = track.toIntOrNull()
            val j = jockey.toIntOrNull()
            val ok = t != null && t in 500..4000 && j != null && j in 500..15000
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(track, { v -> track = v.filter { it.isDigit() }.take(5) }, label = { Text("Wheels apart") },
                    suffix = { Text("mm") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f))
                OutlinedTextField(jockey, { v -> jockey = v.filter { it.isDigit() }.take(5) }, label = { Text("Axle to jockey") },
                    suffix = { Text("mm") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.weight(1f))
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = { onDims(t!!, j!!) }, enabled = live && ok && (t != l.trackMm || j != l.jockeyMm)) { Text("Save") }
        }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun Confirm(text: String, yes: String, onYes: () -> Unit, onNo: () -> Unit) {
    Text(text, color = Accent.warn)
    Spacer(Modifier.height(8.dp))
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(onClick = onYes) { Text(yes) }
        OutlinedButton(onClick = onNo) { Text("Cancel") }
    }
}

/** Top view of the van (hitch at the top) with a bubble that floats to the HIGH side, like a spirit level. */
@Composable
private fun Bubble(l: LevelData, show: Boolean, modifier: Modifier) {
    val bubbleColor = when {
        !show -> Accent.muted
        l.rollOk && l.pitchOk -> Accent.battery
        abs(l.roll) < 1 && abs(l.pitch) < 1 -> Accent.warn
        else -> Accent.alarm
    }
    Canvas(modifier) {
        val c = Offset(size.width / 2, size.height / 2)
        val r = size.minDimension / 2 * 0.95f
        // Van body and A-frame
        val vw = r * 0.62f
        val vh = r * 0.95f
        drawRoundRect(Surfaces.raised, Offset(c.x - vw / 2, c.y - vh / 2 + r * 0.08f), Size(vw, vh), CornerRadius(r * 0.08f))
        val a = Path().apply {
            moveTo(c.x - vw * 0.32f, c.y - vh / 2 + r * 0.08f)
            lineTo(c.x, c.y - r * 0.78f)
            lineTo(c.x + vw * 0.32f, c.y - vh / 2 + r * 0.08f)
        }
        drawPath(a, Accent.muted, style = Stroke(width = 3f))
        // Rings at 1, 2 and 3 degrees
        for (k in 1..3) drawCircle(if (k == 1) Accent.lime.copy(alpha = 0.7f) else Accent.muted.copy(alpha = 0.5f),
            r * k / 3f, c, style = Stroke(width = if (k == 1) 3f else 2f))
        drawLine(Accent.muted.copy(alpha = 0.4f), Offset(c.x - r, c.y), Offset(c.x + r, c.y), 1.5f)
        drawLine(Accent.muted.copy(alpha = 0.4f), Offset(c.x, c.y - r), Offset(c.x, c.y + r), 1.5f)
        if (show) {
            val pxPerDeg = r / DEG_FULL.toFloat()
            var bx = (-l.roll * pxPerDeg).toFloat()
            var by = (-l.pitch * pxPerDeg).toFloat()
            val d = sqrt(bx * bx + by * by)
            if (d > r) { bx *= r / d; by *= r / d }
            drawCircle(bubbleColor, r * 0.13f, Offset(c.x + bx, c.y + by))
        }
    }
}
