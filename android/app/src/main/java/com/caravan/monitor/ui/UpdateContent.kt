package com.caravan.monitor.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.caravan.monitor.UpdState
import com.caravan.monitor.UpdateInfo

/** Slim card at the top of Home while a newer app build is waiting. */
@Composable
fun UpdateBanner(u: UpdateInfo, onUpdate: () -> Unit) {
    if (!u.offer) return
    SectionCard("App update", Accent.battery, compact = true, trailing = {
        Button(onClick = onUpdate, enabled = u.state != UpdState.DOWNLOADING) {
            Text(when (u.state) {
                UpdState.DOWNLOADING -> "${(u.progress * 100).toInt()}%"
                UpdState.READY -> "Install"
                else -> "Update"
            })
        }
    }) {
        Text("Build ${u.latestBuild} is ready" + if (u.notes.isNotBlank()) ": ${u.notes}" else "",
            style = MaterialTheme.typography.bodyMedium, maxLines = 2)
        if (u.message.isNotBlank()) Hint(u.message)
    }
}

/** "App updates" section of Setup. */
@Composable
fun UpdateSection(
    u: UpdateInfo,
    tokenSet: Boolean,
    onSaveToken: (String) -> Unit,
    onCheck: () -> Unit,
    onUpdate: () -> Unit,
    onOpenTokenPage: () -> Unit,
) {
    var token by remember { mutableStateOf("") }
    var editing by remember(tokenSet) { mutableStateOf(!tokenSet) }
    SectionCard("App updates", Accent.battery) {
        Text(
            when (u.state) {
                UpdState.NO_TOKEN -> "Add a GitHub token to get updates in the app."
                UpdState.IDLE -> "This is build ${u.currentBuild}."
                UpdState.CHECKING -> "Checking GitHub…"
                UpdState.UP_TO_DATE -> "Up to date (build ${u.currentBuild})."
                UpdState.AVAILABLE -> "Build ${u.latestBuild} is ready (you have ${u.currentBuild})."
                UpdState.DOWNLOADING -> "Downloading build ${u.latestBuild}… ${(u.progress * 100).toInt()}%"
                UpdState.READY -> "Build ${u.latestBuild} downloaded."
                UpdState.ERROR -> u.message
            },
            color = when (u.state) {
                UpdState.ERROR -> Accent.warn
                UpdState.AVAILABLE, UpdState.READY -> Accent.battery
                else -> Surfaces.text
            },
        )
        if (u.state != UpdState.ERROR && u.message.isNotBlank()) Hint(u.message)
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            when (u.state) {
                UpdState.AVAILABLE -> Button(onClick = onUpdate) { Text("Update") }
                UpdState.READY -> Button(onClick = onUpdate) { Text("Install") }
                else -> if (tokenSet) FilledTonalButton(onClick = onCheck,
                    enabled = u.state != UpdState.CHECKING && u.state != UpdState.DOWNLOADING) { Text("Check now") }
            }
            if (tokenSet && !editing) TextButton(onClick = { editing = true }) { Text("Change token") }
        }
        if (editing) {
            Spacer(Modifier.height(8.dp))
            Text("The app's code is in a private GitHub repository, so downloading updates needs a read-only token. " +
                "Create a fine-grained token for only beau-shelley/Caravan-Power with Contents: Read-only, then paste it here. " +
                "It stays on this phone.", style = MaterialTheme.typography.bodySmall, color = Accent.muted)
            TextButton(onClick = onOpenTokenPage) { Text("Open GitHub token page") }
            OutlinedTextField(token, { token = it.trim() }, label = { Text("GitHub token") }, singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth())
            Row {
                Button(enabled = token.isNotBlank(), onClick = { onSaveToken(token); token = ""; editing = false }) { Text("Save") }
                if (tokenSet) {
                    Spacer(Modifier.width(8.dp))
                    TextButton(onClick = { editing = false; token = "" }) { Text("Cancel") }
                }
            }
        }
    }
}
