package com.caravan.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.caravan.monitor.ActiveAlert
import com.caravan.monitor.CaravanApp
import com.caravan.monitor.HubClient
import com.caravan.monitor.Severity

@Composable
fun SectionCard(title: String, color: Color, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
        shape = RoundedCornerShape(16.dp),
    ) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(8.dp).clip(CircleShape).background(color))
                Spacer(Modifier.width(8.dp))
                Text(title.uppercase(), style = MaterialTheme.typography.labelMedium, color = Accent.muted)
            }
            Spacer(Modifier.height(10.dp))
            content()
        }
    }
}

/** Thin strip showing whether the app is talking to the hub. */
@Composable
fun ConnectionStrip(onOpenSetup: (() -> Unit)? = null) {
    val st by CaravanApp.instance.hub.status.collectAsStateWithLifecycle()
    if (st.conn == HubClient.Conn.CONNECTED) return
    val (color, text) = when (st.conn) {
        HubClient.Conn.AUTH_FAILED -> Accent.alarm to "${st.detail}. Check the login in Setup."
        HubClient.Conn.SEARCHING, HubClient.Conn.CONNECTING, HubClient.Conn.IDLE -> Accent.muted to (st.detail.ifBlank { "Connecting…" })
        else -> Accent.warn to st.detail.ifBlank { "Not connected" }
    }
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(color.copy(alpha = 0.15f)).padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, color = color, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        if (onOpenSetup != null && st.conn != HubClient.Conn.CONNECTING && st.conn != HubClient.Conn.SEARCHING) {
            TextButton(onClick = onOpenSetup) { Text("Setup") }
        }
    }
}

fun sevColor(sev: Int) = when (sev) { Severity.ALARM -> Accent.alarm; Severity.WARN -> Accent.warn; else -> Accent.water }

@Composable
fun AlertRow(a: ActiveAlert, title: String?) {
    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(10.dp).clip(CircleShape).background(if (a.ack) Accent.muted else sevColor(a.sev)))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(title ?: Severity.label(a.sev), fontWeight = FontWeight.SemiBold)
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
