package com.caravan.monitor.ui

import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color

// Caravan theme background. Dark, as on the touchscreen, with a faint lime glow in the
// top corner and a faint blue one at the bottom (design 2026-11-26). The camo shapes stay
// in CamoPaths.kt (tools/theme/make_theme.py) for later use as a subtle highlight.

private val bgTop = Color(0xFF131518)
private val bgBottom = Color(0xFF0A0B0D)

/** App background: dark vertical gradient with two soft glows. Static, drawn once per frame of the scaffold. */
fun Modifier.caravanBackground(): Modifier = this.drawBehind {
    drawRect(Brush.verticalGradient(listOf(bgTop, bgBottom)))
    val r1 = size.width * 1.1f
    drawRect(Brush.radialGradient(listOf(Accent.lime.copy(alpha = 0.11f), Color.Transparent), Offset(0f, 0f), r1))
    val r2 = size.width * 0.9f
    drawRect(Brush.radialGradient(listOf(Accent.water.copy(alpha = 0.06f), Color.Transparent), Offset(size.width, size.height), r2))
}
