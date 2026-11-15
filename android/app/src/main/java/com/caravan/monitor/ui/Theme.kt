package com.caravan.monitor.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Same accent colours as the touchscreen and web page.
object Accent {
    val battery = Color(0xFF4CC38A)
    val solar = Color(0xFFF5B841)
    val water = Color(0xFF4DA3FF)
    val climate = Color(0xFFB48CFF)
    val warn = Color(0xFFFFA94D)
    val alarm = Color(0xFFFF5C5C)
    val muted = Color(0xFF8A96A3)
}

private val Scheme = darkColorScheme(
    primary = Color(0xFF4DA3FF),
    onPrimary = Color(0xFF00213F),
    secondary = Accent.battery,
    background = Color(0xFF101418),
    surface = Color(0xFF101418),
    surfaceContainer = Color(0xFF1A2027),
    surfaceContainerHigh = Color(0xFF222A33),
    surfaceContainerHighest = Color(0xFF2A333D),
    error = Accent.alarm,
)

@Composable
fun CaravanTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = Scheme, content = content)
}
