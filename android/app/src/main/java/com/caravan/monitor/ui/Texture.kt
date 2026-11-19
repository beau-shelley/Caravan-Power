package com.caravan.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.ui.Modifier

// Caravan theme background. Plain dark (the X graphic and chequer plate were
// taken out at Beau's request); the camo shapes stay in CamoPaths.kt
// (tools/theme/make_theme.py) for later use as a subtle highlight.

/** Plain dark app background, the same colour as the touchscreen. */
fun Modifier.caravanBackground(): Modifier = this.background(Surfaces.background)
