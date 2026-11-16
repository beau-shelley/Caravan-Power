package com.caravan.monitor.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import com.caravan.monitor.Severity

// Same accent colours as the touchscreen and web page.
object Accent {
    val battery = Color(0xFF4CC38A)
    val solar = Color(0xFFF5B841)
    val water = Color(0xFF4DA3FF)
    val mains = Color(0xFF60A5FA)
    val gas = Color(0xFFFF8A3D)
    val climate = Color(0xFFB48CFF)
    val warn = Color(0xFFFFA94D)
    val alarm = Color(0xFFFF5C5C)
    val critical = Color(0xFFD946EF)  // fuchsia, same as the touchscreen and web page
    val muted = Color(0xFF8A96A3)
}

/** Background and card colours (dark theme). */
object Surfaces {
    val background = Color(0xFF101418)
    val card = Color(0xFF1A2027)
    val raised = Color(0xFF222A33)
    val track = Color(0xFF2A333D)
    val text = Color(0xFFE6EAEE)
}

fun sevColor(sev: Int) = when {
    sev >= Severity.CRIT -> Accent.critical
    sev == Severity.ALARM -> Accent.alarm
    sev == Severity.WARN -> Accent.warn
    else -> Accent.water
}

fun socColor(soc: Double) = when { soc < 20 -> Accent.alarm; soc < 40 -> Accent.warn; else -> Accent.battery }

/** Gas bottle: amber below 25 %, red below 10 % (same as the touchscreen). */
fun gasColor(pct: Double) = when { pct < 10 -> Accent.alarm; pct < 25 -> Accent.warn; else -> Accent.gas }

// Temperature colour blended between stops (same as the touchscreen and web page).
// Air: blue at 10 C and below, neutral 17-23, amber 27, strong red 30+.
// Fridge (index 2): blue at 0 (freezing), neutral 2-5 (safe), amber 6.5, red 8+.
fun tempColor(index: Int, c: Double, neutral: Color = Surfaces.text): Color {
    val stops = if (index == 2)
        listOf(0.0 to Accent.water, 2.0 to neutral, 5.0 to neutral, 6.5 to Accent.warn, 8.0 to Accent.alarm)
    else
        listOf(10.0 to Accent.water, 17.0 to neutral, 23.0 to neutral, 27.0 to Accent.warn, 30.0 to Accent.alarm)
    if (c <= stops.first().first) return stops.first().second
    for (k in 1 until stops.size) {
        val (t1, c1) = stops[k]
        if (c > t1) continue
        val (t0, c0) = stops[k - 1]
        return lerp(c0, c1, ((c - t0) / (t1 - t0)).toFloat())
    }
    return stops.last().second
}
