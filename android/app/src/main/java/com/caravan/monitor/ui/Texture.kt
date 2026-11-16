package com.caravan.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.vector.PathParser

// Caravan textures, drawn rather than loaded so they look sharp at any size and
// work the same in the app and the screenshot renderer. The camo comes from
// tools/theme/make_theme.py (CamoPaths.kt), same tile as the touchscreen and web page.

private val camoPaths: List<Path> by lazy { CAMO_PATHS.map { PathParser().parsePathString(it).toPath() } }

/** Subtle dark camo, tiled. [tileDp] is the size of one tile on screen. */
fun Modifier.camoBackground(tileDp: Float = 300f): Modifier = this
    .background(Surfaces.background)
    .drawWithCache {
        val tile = tileDp * density
        val k = tile / CAMO_TILE
        onDrawBehind {
            clipRect {
                var y = 0f
                while (y < size.height) {
                    var x = 0f
                    while (x < size.width) {
                        withTransform({ translate(x, y); scale(k, k, Offset.Zero) }) {
                            camoPaths.forEachIndexed { i, p -> drawPath(p, Surfaces.camo[i]) }
                        }
                        x += tile
                    }
                    y += tile
                }
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
