package com.caravan.monitor.ui

// Gradients, glows and the few small animations used across the app (design approved
// by Beau 2026-11-26). Everything animated here only redraws (no recomposition per
// frame), and the endless ones only run while something is actually happening
// (charging, a pump running, the heater burning), so they cost next to nothing.

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/** Card and tile colours for the gradient look. */
object Grad {
    val cardTop = Color(0xFF34383D)
    val cardBottom = Color(0xFF212428)
    val card = Brush.verticalGradient(listOf(cardTop, cardBottom))
    val edge = Color.White.copy(alpha = 0.06f)
    /** Tiles and chips sunk into a card. */
    val chip = Color.Black.copy(alpha = 0.22f)
    val chipEdge = Color.White.copy(alpha = 0.04f)
    val track = Color(0xFF17191C)
}

/** A lighter tint of [c] (the top / start of its gradient). */
fun light(c: Color, by: Float = 0.38f) = lerp(c, Color.White, by)
/** A deeper shade of [c] (the bottom / end of its gradient). */
fun deep(c: Color, by: Float = 0.28f) = lerp(c, Color.Black, by)
/** Top-to-bottom gradient for big numbers. */
fun vGrad(c: Color) = Brush.verticalGradient(listOf(light(c), c))
/** Left-to-right gradient for bars and buttons. */
fun hGrad(c: Color) = Brush.horizontalGradient(listOf(light(c, 0.3f), c, deep(c, 0.12f)))

/** Text painted with a gradient (big readings). */
@OptIn(ExperimentalTextApi::class)
@Composable
fun GradText(
    text: String, color: Color, fontSize: TextUnit, modifier: Modifier = Modifier,
    weight: FontWeight = FontWeight.ExtraBold, letterSpacing: TextUnit = (-0.5).sp,
) {
    Text(text, modifier, style = MaterialTheme.typography.bodyLarge.merge(
        TextStyle(brush = vGrad(color), fontSize = fontSize, fontWeight = weight, letterSpacing = letterSpacing)))
}

/** A big reading with a smaller unit after it, both in the gradient, e.g. "18.4 A". */
@Composable
fun GradValue(value: String, unit: String, color: Color, size: TextUnit, modifier: Modifier = Modifier) {
    Row(modifier, verticalAlignment = Alignment.Bottom) {
        GradText(value, color, size, Modifier.alignByBaseline())
        if (unit.isNotEmpty()) GradText(" $unit", color, size * 0.55f, Modifier.alignByBaseline(), FontWeight.Bold, 0.sp)
    }
}

/** Small rounded square holding a section icon, tinted with the section colour. */
@Composable
fun IconBadge(color: Color, content: @Composable BoxScope.() -> Unit) {
    Box(Modifier.size(24.dp).clip(RoundedCornerShape(8.dp)).background(color.copy(alpha = 0.17f)),
        contentAlignment = Alignment.Center, content = content)
}

/** Status pill (e.g. "Charging", "Running"), tinted with [color]. */
@Composable
fun Pill(text: String, color: Color, modifier: Modifier = Modifier) {
    Text(text, modifier.clip(CircleShape).background(color.copy(alpha = 0.15f)).padding(horizontal = 10.dp, vertical = 3.dp),
        color = light(color, 0.25f), style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.SemiBold)
}

/** A sunk-in tile inside a card (stats, temperatures). [tint] washes it with a colour. */
fun Modifier.chip(tint: Color? = null, radius: Dp = 14.dp): Modifier = this
    .clip(RoundedCornerShape(radius))
    .background(if (tint == null) Brush.verticalGradient(listOf(Color.White.copy(alpha = 0.05f), Grad.chip))
                else Brush.verticalGradient(listOf(tint.copy(alpha = 0.16f), Grad.chip)))
    .border(1.dp, Grad.chipEdge, RoundedCornerShape(radius))

/** A soft round glow behind a card's corner. [pulse] (0..1) breathes it, read at draw time only. */
fun Modifier.cornerGlow(color: Color, alpha: Float = 0.18f, atX: Float = 1f, atY: Float = 0f, radius: Dp = 110.dp,
                        pulse: State<Float>? = null): Modifier = drawBehind {
    val r = radius.toPx()
    val a = alpha * (pulse?.let { 0.6f + 0.4f * it.value } ?: 1f)
    val c = Offset(size.width * atX, size.height * atY)
    drawCircle(Brush.radialGradient(listOf(color.copy(alpha = a), Color.Transparent), c, r), r, c)
}

/** An endless 0..1..0 value for gentle pulses (only create it while it's needed). */
@Composable
fun pulse(periodMs: Int = 2200): State<Float> =
    rememberInfiniteTransition(label = "pulse").animateFloat(0f, 1f,
        infiniteRepeatable(tween(periodMs, easing = FastOutSlowInEasing), RepeatMode.Reverse), label = "pulse")

/** An endless 0..1 ramp (orbits, waves). */
@Composable
fun ramp(periodMs: Int): State<Float> =
    rememberInfiniteTransition(label = "ramp").animateFloat(0f, 1f,
        infiniteRepeatable(tween(periodMs, easing = LinearEasing)), label = "ramp")

private val still = mutableStateOf(0f)

/**
 * Round gauge: a dark track with a gradient arc from 12 o'clock for [fraction] (0..1).
 * The arc eases to new values; with [spark] a bright dot runs along it (charging).
 */
@Composable
fun RingGauge(
    fraction: Double, color: Color, modifier: Modifier = Modifier, stroke: Dp = 12.dp, spark: Boolean = false,
    content: @Composable BoxScope.() -> Unit = {},
) {
    val f by animateFloatAsState(fraction.toFloat().coerceIn(0f, 1f), tween(900, easing = FastOutSlowInEasing), label = "ring")
    val orbit = if (spark) ramp(2600) else still
    val c0 = light(color, 0.45f); val c1 = color; val c2 = deep(color, 0.3f)
    Box(modifier.drawBehind {
        val sw = stroke.toPx()
        val tl = Offset(sw / 2 + sw * 0.4f, sw / 2 + sw * 0.4f)
        val arc = Size(size.width - 2 * tl.x, size.height - 2 * tl.y)
        drawArc(Grad.track, 0f, 360f, false, tl, arc, style = Stroke(sw))
        val v = f
        if (v > 0.004f) {
            // Deep at the start (12 o'clock), bright at the head.
            val brush = Brush.sweepGradient(0f to c2, (v * 0.55f) to c1, v to c0, 1f to c0, center = center)
            rotate(-90f) {
                drawArc(brush, 0f, 360f * v, false, tl, arc, alpha = 0.12f, style = Stroke(sw * 1.8f, cap = StrokeCap.Round))
                drawArc(brush, 0f, 360f * v, false, tl, arc, style = Stroke(sw, cap = StrokeCap.Round))
            }
            // The start cap would take the sweep's end colour; paint it in the start colour.
            drawCircle(c2, sw / 2, Offset(center.x, tl.y))
            if (spark) {
                val a = (-90f + 360f * v * orbit.value) * (PI / 180f).toFloat()
                val r = arc.width / 2
                val p = Offset(center.x + r * cos(a), center.y + r * sin(a))
                drawCircle(Color.White.copy(alpha = 0.35f), sw * 0.75f, p)
                drawCircle(Color.White.copy(alpha = 0.95f), sw * 0.32f, p)
            }
        }
    }, contentAlignment = Alignment.Center, content = content)
}

/** Horizontal level bar 0..1 with a gradient fill that eases to new values. */
@Composable
fun GradBar(fraction: Double, color: Color, modifier: Modifier = Modifier, height: Dp = 8.dp) {
    val f by animateFloatAsState(fraction.toFloat().coerceIn(0f, 1f), tween(800, easing = FastOutSlowInEasing), label = "bar")
    Canvas(modifier.fillMaxWidth().height(height)) {
        val r = CornerRadius(size.height / 2, size.height / 2)
        drawRoundRect(Grad.track, cornerRadius = r)
        if (f > 0.003f) {
            val w = (size.width * f).coerceAtLeast(size.height)
            drawRoundRect(Brush.horizontalGradient(listOf(light(color, 0.3f), color), 0f, w), size = Size(w, size.height), cornerRadius = r)
            // soft highlight near the leading edge
            drawRoundRect(Brush.horizontalGradient(listOf(Color.Transparent, Color.White.copy(alpha = 0.35f), Color.Transparent),
                w * 0.55f, w), size = Size(w, size.height), cornerRadius = r)
        }
    }
}

/**
 * A tank seen from the front: rounded box filled to [pct] with a gradient and a wavy
 * surface. The wave drifts slowly when [animate] (Home only), else it's still.
 */
@Composable
fun WaveTank(pct: Double, color: Color, modifier: Modifier = Modifier, animate: Boolean = true) {
    val f by animateFloatAsState((pct / 100.0).toFloat().coerceIn(0f, 1f), tween(900, easing = FastOutSlowInEasing), label = "tank")
    val phase = if (animate) ramp(3600) else still
    Box(modifier, contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val r = CornerRadius(14.dp.toPx(), 14.dp.toPx())
            val box = Path().apply { addRoundRect(androidx.compose.ui.geometry.RoundRect(0f, 0f, size.width, size.height, r)) }
            clipPath(box) {
                drawRect(Grad.track)
                val v = f
                if (v > 0.002f) {
                    val top = size.height * (1 - v)
                    val amp = 3.dp.toPx() * (if (v > 0.97f) 0.3f else 1f)
                    val ph = phase.value * 2 * PI.toFloat()
                    val wave = Path().apply {
                        moveTo(0f, size.height)
                        lineTo(0f, top)
                        var x = 0f
                        val step = size.width / 16
                        while (x <= size.width + 0.5f) {
                            lineTo(x, top + amp * sin(ph + x / size.width * 2 * PI.toFloat()))
                            x += step
                        }
                        lineTo(size.width, size.height)
                        close()
                    }
                    drawPath(wave, Brush.verticalGradient(listOf(light(color, 0.35f), deep(color, 0.15f)), top - amp, size.height))
                    // surface highlight
                    val line = Path().apply {
                        var x = 0f
                        val step = size.width / 16
                        moveTo(0f, top + amp * sin(ph))
                        while (x <= size.width + 0.5f) {
                            lineTo(x, top + amp * sin(ph + x / size.width * 2 * PI.toFloat()))
                            x += step
                        }
                    }
                    drawPath(line, Color.White.copy(alpha = 0.45f), style = Stroke(1.5.dp.toPx()))
                }
            }
        }
    }
}

/** "›››" that shimmer left to right (slide-to-arm hint). */
@Composable
fun ShimmerChevrons(color: Color) {
    val t = ramp(1600)
    Row {
        for (k in 0 until 3) {
            Text("›", color = color, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.graphicsLayer {
                    val d = ((t.value * 3f - k) % 3f + 3f) % 3f   // 0..3, 0 = this chevron is lit
                    alpha = 0.3f + 0.7f * (1f - (d.coerceAtMost(1f)))
                })
        }
    }
}

/** Small dot that breathes (the "Live" pill). */
@Composable
fun LiveDot(color: Color, animate: Boolean = true) {
    val p = if (animate) pulse(1400) else still
    Box(Modifier.size(16.dp), contentAlignment = Alignment.Center) {
        Box(Modifier.size(16.dp).graphicsLayer { alpha = 0.15f + 0.25f * p.value; scaleX = 0.7f + 0.3f * p.value; scaleY = scaleX }
            .clip(CircleShape).background(color))
        Box(Modifier.size(8.dp).clip(CircleShape).background(color))
    }
}

/** Header at the top of Home and Climate: big title, a subtitle and the live pill. */
@Composable
fun ScreenHeader(title: String, subtitle: String, live: String?) {
    Row(Modifier.padding(start = 4.dp, end = 2.dp, bottom = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title, fontSize = 24.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = (-0.4).sp)
            if (subtitle.isNotBlank()) Text(subtitle, style = MaterialTheme.typography.bodySmall, color = Accent.muted, maxLines = 1)
        }
        if (live != null) {
            Row(
                Modifier.clip(CircleShape).background(Accent.lime.copy(alpha = 0.12f))
                    .border(1.dp, Accent.lime.copy(alpha = 0.3f), CircleShape).padding(start = 8.dp, end = 12.dp, top = 5.dp, bottom = 5.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                LiveDot(Accent.lime)
                Spacer(Modifier.width(6.dp))
                Text(live, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.SemiBold, color = light(Accent.lime, 0.3f))
            }
        }
    }
}

/** Row of equal chips, e.g. Charging / Load. */
@Composable
fun ChipRow(content: @Composable RowScope.() -> Unit) =
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), content = content)
