package com.caravan.monitor.ui

import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.layout.ParentDataModifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

// Makes the app fill the phone's screen whatever its size: everything is scaled
// to the screen width, and Home's cards grow to use any spare height.

/** Width the layout was designed at; narrower or wider phones are scaled to match. */
private const val DESIGN_WIDTH_DP = 400f

/** Scales dp and text so the layout looks the same on any phone width (within limits, so
 *  tablets and tiny phones stay readable). */
@Composable
fun ScaleToScreen(content: @Composable () -> Unit) {
    BoxWithConstraints {
        val d = LocalDensity.current
        val k = (maxWidth.value / DESIGN_WIDTH_DP).coerceIn(0.85f, 1.35f)
        CompositionLocalProvider(LocalDensity provides Density(d.density * k, d.fontScale)) { content() }
    }
}

private class Grow(val weight: Float) : ParentDataModifier {
    override fun Density.modifyParentData(parentData: Any?) = this@Grow
}

/** In a [FillColumn], this child takes a share (by [weight]) of any spare height. */
fun Modifier.grow(weight: Float = 1f): Modifier = this.then(Grow(weight))

/**
 * A column that stacks its children with [gap] between them and, when they don't fill
 * [minHeight], stretches the children marked [grow] to fill it. Taller content just
 * grows past it (put it in a scroll).
 */
@Composable
fun FillColumn(minHeight: Dp, modifier: Modifier = Modifier, gap: Dp = 8.dp, content: @Composable () -> Unit) {
    Layout(content, modifier) { ms, c ->
        val loose = c.copy(minHeight = 0, maxHeight = androidx.compose.ui.unit.Constraints.Infinity)
        val gapPx = gap.roundToPx()
        val weights = ms.map { (it.parentData as? Grow)?.weight ?: 0f }
        val total = weights.sum()
        // Natural heights first (intrinsics, so each child is measured only once).
        val natural = ms.map { it.maxIntrinsicHeight(c.maxWidth) }
        val used = natural.sum() + gapPx * (ms.size - 1).coerceAtLeast(0)
        val spare = (minHeight.roundToPx() - used).coerceAtLeast(0)
        val placed = ms.mapIndexed { i, m ->
            if (spare == 0 || total == 0f || weights[i] == 0f) m.measure(loose)
            else {
                val h = natural[i] + (spare * weights[i] / total).toInt()
                m.measure(c.copy(minHeight = h, maxHeight = h))
            }
        }
        val h = maxOf(placed.sumOf { it.height } + gapPx * (placed.size - 1).coerceAtLeast(0), c.minHeight)
        layout(c.maxWidth, h) {
            var y = 0
            placed.forEach { it.place(0, y); y += it.height + gapPx }
        }
    }
}
