package com.caravan.monitor.ui

// Building blocks shared by every screen. Nothing in the ui/*Content.kt files
// or here touches Android APIs or the hub connection, so the same code renders
// the screenshots in android/screenshots/ on a desktop JVM.

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.caravan.monitor.ActiveAlert
import com.caravan.monitor.Conn
import com.caravan.monitor.ConnStatus
import com.caravan.monitor.DataAge
import androidx.compose.material.icons.filled.CloudOff
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ColorMatrix
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import com.caravan.monitor.Severity
import java.util.Locale

@Composable
fun SectionCard(
    title: String, color: Color, modifier: Modifier = Modifier, icon: ImageVector? = null,
    compact: Boolean = false,
    trailing: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Surfaces.glass),
        shape = RoundedCornerShape(16.dp),
    ) {
        // fillMaxHeight only matters when the card is stretched (Home on a tall phone):
        // the rows then spread out evenly instead of leaving a gap at the bottom.
        Column(Modifier.fillMaxHeight().padding(if (compact) 12.dp else 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (icon != null) Icon(icon, null, Modifier.size(16.dp), tint = color)
                else Box(Modifier.size(8.dp).clip(CircleShape).background(color))
                Spacer(Modifier.width(if (icon != null) 6.dp else 8.dp))
                Text(title.uppercase(), style = MaterialTheme.typography.labelMedium, color = Accent.muted,
                    modifier = Modifier.weight(1f))
                trailing?.invoke()
            }
            Spacer(Modifier.height(if (compact) 6.dp else 10.dp))
            Column(Modifier.fillMaxHeight(), verticalArrangement = Arrangement.SpaceEvenly) { content() }
        }
    }
}

/** Thin strip showing whether the app is talking to the hub. Hidden while connected direct;
 *  through the cloud it says so, since updates are slower (every 15 s) and some controls may be off. */
@Composable
fun ConnectionStrip(st: ConnStatus, onOpenSetup: (() -> Unit)? = null) {
    if (st.conn == Conn.CONNECTED && !st.cloud) return
    val (color, text) = when (st.conn) {
        Conn.CONNECTED -> (if (st.detail.contains("offline")) Accent.warn else Accent.water) to "Away from the van · ${st.detail.replaceFirstChar { it.lowercase() }}"
        Conn.AUTH_FAILED -> Accent.alarm to "${st.detail}. Check the login in Setup."
        Conn.SEARCHING, Conn.CONNECTING, Conn.IDLE -> Accent.muted to (st.detail.ifBlank { "Connecting…" })
        else -> Accent.warn to st.detail.ifBlank { "Not connected" }
    }
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(color.copy(alpha = 0.15f))
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, color = color, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        if (onOpenSetup != null && st.conn != Conn.CONNECTING && st.conn != Conn.SEARCHING && st.conn != Conn.CONNECTED) {
            TextButton(onClick = onOpenSetup) { Text("Setup") }
        }
    }
}

/** One active alert. An unacknowledged critical one gets a tinted fill and a CRITICAL tag. */
@Composable
fun AlertRow(a: ActiveAlert, title: String?) {
    val crit = a.sev >= Severity.CRIT && !a.ack
    Row(
        Modifier.fillMaxWidth().padding(vertical = if (crit) 4.dp else 0.dp).clip(RoundedCornerShape(12.dp))
            .background(if (crit) Accent.critical.copy(alpha = 0.18f) else Color.Transparent)
            .padding(horizontal = if (crit) 10.dp else 0.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(10.dp).clip(CircleShape).background(if (a.ack) Accent.muted else sevColor(a.sev)))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (crit) {
                    Text("CRITICAL", style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold,
                        color = Color.White, modifier = Modifier.clip(RoundedCornerShape(4.dp))
                            .background(Accent.critical).padding(horizontal = 5.dp, vertical = 1.dp))
                    Spacer(Modifier.width(6.dp))
                }
                Text(title ?: Severity.label(a.sev), fontWeight = FontWeight.SemiBold)
            }
            Text(a.msg, style = MaterialTheme.typography.bodySmall, color = Accent.muted)
        }
        if (a.ack) Text("seen", style = MaterialTheme.typography.labelSmall, color = Accent.muted)
    }
}

@Composable
fun Stat(label: String, value: String, modifier: Modifier = Modifier, valueColor: Color = Color.Unspecified) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = Accent.muted)
        Text(value, style = MaterialTheme.typography.titleMedium, color = valueColor)
    }
}

/** Horizontal level bar, 0..1. */
@Composable
fun Bar(fraction: Double, color: Color, modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth().height(8.dp).clip(RoundedCornerShape(4.dp)).background(Surfaces.track)) {
        Box(Modifier.fillMaxHeight().fillMaxWidth(fraction.toFloat().coerceIn(0f, 1f)).clip(RoundedCornerShape(4.dp)).background(color))
    }
}

@Composable
fun SwitchRow(label: String, checked: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled)
    }
}

@Composable
fun Field(label: String, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = Accent.muted)
        content()
    }
}

@Composable
fun <T> Picker(value: String, options: List<Pair<T, String>>, onPick: (T) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { open = true }, modifier = Modifier.fillMaxWidth()) {
            Text(value, modifier = Modifier.weight(1f))
            Icon(Icons.Filled.ArrowDropDown, null)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { (k, label) ->
                DropdownMenuItem(text = { Text(label) }, onClick = { open = false; onPick(k) })
            }
        }
    }
}

@Composable
fun Hint(text: String) = Text(text, style = MaterialTheme.typography.bodySmall, color = Accent.muted)

fun fmt1(v: Double) = String.format(Locale.US, "%.1f", v)
fun fmt2(v: Double) = String.format(Locale.US, "%.2f", v)
// Temperatures are shown as whole degrees (rounded; never "-0").
fun fmtDeg(v: Double) = Math.round(v).toInt().toString()
fun signed(v: Double) = (if (v > 0) "+" else "") + fmt1(v)

/**
 * Shown instead of [ConnectionStrip] whenever the data isn't live: a clear "No connection"
 * banner saying why and how old the greyed-out values below it are.
 */
@Composable
fun NoConnectionBanner(age: DataAge, status: ConnStatus, onOpenSetup: (() -> Unit)? = null, now: Long = System.currentTimeMillis()) {
    val color = if (status.conn == Conn.AUTH_FAILED) Accent.alarm else Accent.warn
    val ago = DataAge.ago(age.lastAt, now)
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(color.copy(alpha = 0.18f))
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Icons.Filled.CloudOff, null, Modifier.size(22.dp), tint = color)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text("No connection", color = color, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            val why = if (status.conn == Conn.AUTH_FAILED) "${age.why}. Check the login in Setup." else age.why
            if (why.isNotBlank()) Text(why, color = Surfaces.text, style = MaterialTheme.typography.bodySmall)
            Text(
                if (ago.isNotEmpty()) "Greyed-out values are from $ago and may be wrong." else "Greyed-out values are old and may be wrong.",
                color = Accent.muted, style = MaterialTheme.typography.bodySmall,
            )
        }
        if (onOpenSetup != null && status.conn != Conn.CONNECTING && status.conn != Conn.SEARCHING && status.conn != Conn.CONNECTED) {
            TextButton(onClick = onOpenSetup) { Text("Setup") }
        }
    }
}

private val greyPaint = Paint().apply {
    colorFilter = ColorFilter.colorMatrix(ColorMatrix().apply { setToSaturation(0f) })
    alpha = 0.38f
}

/** Greys out (no colour, faded) and blocks taps on everything inside, while the data isn't live. */
fun Modifier.greyedOut(on: Boolean): Modifier = if (!on) this else this
    .drawWithContent {
        drawIntoCanvas { c ->
            c.saveLayer(Rect(Offset.Zero, size), greyPaint)
            drawContent()
            c.restore()
        }
    }
    .pointerInput(Unit) {
        // Swallow every touch: the controls would act on old values.
        awaitPointerEventScope {
            while (true) awaitPointerEvent(PointerEventPass.Initial).changes.forEach { it.consume() }
        }
    }
