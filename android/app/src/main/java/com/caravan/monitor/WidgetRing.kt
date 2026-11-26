package com.caravan.monitor

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.DashPathEffect
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.SweepGradient

/**
 * The widget's battery ring, drawn into a small bitmap (home-screen widgets can't draw
 * arcs or gradients themselves). Same look as the app's ring: a dark track and a
 * gradient arc from 12 o'clock, deep at the start and bright at the head. Not live: a
 * dashed grey track only, so no old value is shown.
 */
object WidgetRing {
    fun bitmap(context: Context, sizeDp: Int, pct: Int, argb: Long, live: Boolean): Bitmap {
        val px = (sizeDp * context.resources.displayMetrics.density).toInt().coerceIn(32, 240)
        val bmp = Bitmap.createBitmap(px, px, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val sw = px * 0.12f
        val r = RectF(sw / 2 + 1, sw / 2 + 1, px - sw / 2 - 1, px - sw / 2 - 1)
        val track = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE; strokeWidth = sw
            this.color = if (live) 0xFF0D0E10.toInt() else 0xFF2E3135.toInt()
            if (!live) pathEffect = DashPathEffect(floatArrayOf(sw * 0.45f, sw * 0.65f), 0f)
        }
        c.drawArc(r, 0f, 360f, false, track)
        if (live && pct > 0) {
            val f = (pct / 100f).coerceIn(0.01f, 1f)
            val base = argb.toInt()
            val deep = blend(base, 0xFF000000.toInt(), 0.3f)
            val light = blend(base, 0xFFFFFFFF.toInt(), 0.45f)
            val cx = px / 2f
            val sweep = SweepGradient(cx, cx, intArrayOf(deep, base, light, light), floatArrayOf(0f, f * 0.55f, f, 1f))
            sweep.setLocalMatrix(Matrix().apply { setRotate(-90f, cx, cx) })
            val arc = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                style = Paint.Style.STROKE; strokeWidth = sw; strokeCap = Paint.Cap.ROUND; shader = sweep
            }
            c.drawArc(r, -90f, 360f * f, false, arc)
            // The round start cap would take the sweep's end colour; paint it in the start colour.
            c.drawCircle(cx, r.top, sw / 2, Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = deep })
        }
        return bmp
    }

    private fun blend(a: Int, b: Int, t: Float): Int {
        fun ch(sh: Int) = (((a shr sh) and 0xFF) * (1 - t) + ((b shr sh) and 0xFF) * t).toInt().coerceIn(0, 255) shl sh
        return (0xFF shl 24) or ch(16) or ch(8) or ch(0)
    }
}
