package com.caravan.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathFillType
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.vector.PathParser

// Caravan art, drawn rather than loaded so it is sharp at any size and works the
// same in the app and the screenshot renderer. Shapes come from
// tools/theme/make_theme.py (CamoPaths.kt): the same camo and X graphic as the
// touchscreen and the web page, traced from the decals on the van.

private fun parse(d: String, evenOdd: Boolean = false): Path =
    PathParser().parsePathString(d).toPath().apply { if (evenOdd) fillType = PathFillType.EvenOdd }

private val camoPaths: List<Path> by lazy { CAMO_PATHS.map { parse(it, evenOdd = true) } }
private val xPieces: Path by lazy { parse(X_PIECES) }
private val xLines: Path by lazy { parse(X_LINES) }

/**
 * Gunmetal background with the van's X graphic (camo-filled, lime pinstripes).
 * The X is [widthFraction] of the screen width wide, its left edge at [left] and
 * its middle at [centerY] (fractions of the screen).
 */
fun Modifier.caravanBackground(
    widthFraction: Float = 1.2f,
    left: Float = -0.06f,
    centerY: Float = 0.5f,
    camoDp: Float = 100f,
): Modifier = this
    .background(Surfaces.background)
    .drawWithCache {
        val s = size.width * widthFraction / X_W
        val x0 = size.width * left
        val y0 = size.height * centerY - X_H * s / 2
        val tile = camoDp * density
        val k = tile / CAMO_TILE
        onDrawBehind {
            withTransform({ translate(x0, y0); scale(s, s, Offset.Zero) }) {
                clipPath(xPieces) {
                    // camo, in screen-sized tiles (undo the X's scale)
                    val t = tile / s
                    drawRect(Surfaces.camo[0], Offset.Zero, androidx.compose.ui.geometry.Size(X_W, X_H))
                    var y = 0f
                    while (y < X_H) {
                        var x = 0f
                        while (x < X_W) {
                            withTransform({ translate(x, y); scale(k / s, k / s, Offset.Zero) }) {
                                camoPaths.forEachIndexed { i, p -> drawPath(p, Surfaces.camo[i + 1]) }
                            }
                            x += t
                        }
                        y += t
                    }
                }
                drawPath(xLines, Accent.lime, style = Stroke(width = X_LINE_W, join = StrokeJoin.Miter, cap = StrokeCap.Butt))
            }
        }
    }

/** Black chequer (diamond) plate, like the van's lower skirt and the touchscreen's nav rail. */
fun Modifier.treadPlate(cellDp: Float = 18f): Modifier = this
    .background(Surfaces.plate)
    .drawWithCache {
        val cell = cellDp * density
        val u = cell / 24f
        val w = 3f * u
        val lozenges = listOf(
            Offset(3f, 9f) to Offset(9f, 3f), Offset(15f, 21f) to Offset(21f, 15f),
            Offset(15f, 3f) to Offset(21f, 9f), Offset(3f, 15f) to Offset(9f, 21f),
        )
        onDrawBehind {
            clipRect {
                var y = 0f
                while (y < size.height) {
                    var x = 0f
                    while (x < size.width) {
                        translate(x, y) {
                            for ((a, b) in lozenges) {
                                val o = -0.6f * u
                                drawLine(Surfaces.ridgeLight, Offset(a.x * u + o, a.y * u + o), Offset(b.x * u + o, b.y * u + o), w, StrokeCap.Round)
                                drawLine(Surfaces.ridge, Offset(a.x * u, a.y * u), Offset(b.x * u, b.y * u), w, StrokeCap.Round)
                            }
                        }
                        x += cell
                    }
                    y += cell
                }
            }
        }
    }
