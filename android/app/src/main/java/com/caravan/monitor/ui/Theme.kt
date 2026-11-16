package com.caravan.monitor.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Scheme = darkColorScheme(
    primary = Color(0xFF4DA3FF),
    onPrimary = Color(0xFF00213F),
    secondary = Accent.battery,
    primaryContainer = Color(0xFF1F3A57),
    onPrimaryContainer = Surfaces.text,
    secondaryContainer = Color(0xFF1F3A57),
    onSecondaryContainer = Surfaces.text,
    background = Surfaces.background,
    surface = Surfaces.background,
    onSurface = Surfaces.text,
    onBackground = Surfaces.text,
    surfaceVariant = Surfaces.raised,
    surfaceContainer = Surfaces.card,
    surfaceContainerHigh = Surfaces.raised,
    surfaceContainerHighest = Surfaces.track,
    error = Accent.alarm,
)

@Composable
fun CaravanTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = Scheme, content = content)
}
