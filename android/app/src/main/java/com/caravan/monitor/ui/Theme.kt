package com.caravan.monitor.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable

/** App theme: caravan colours (see Colors.kt). */
@Composable
fun CaravanTheme(content: @Composable () -> Unit) {
    // surfaceContainer* are newer than the screenshot renderer's Material 3, so they live here.
    val scheme = CaravanColors.copy(
        surfaceContainer = Surfaces.card,
        surfaceContainerHigh = Surfaces.raised,
        surfaceContainerHighest = Surfaces.track,
    )
    MaterialTheme(colorScheme = scheme, content = content)
}
