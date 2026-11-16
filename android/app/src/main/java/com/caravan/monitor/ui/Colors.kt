package com.caravan.monitor.ui

import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import com.caravan.monitor.Severity

// Caravan theme: gunmetal cards on dark camo, black chequer plate, lime accents
// (like the van). Same colours as the touchscreen (ui.h col::) and the web page.
object Accent {
    val lime = Color(0xFFA3E635)  // theme accent: selected tab, primary buttons, info
    val battery = Color(0xFF8FDB3A)
    val solar = Color(0xFFFACC15)
    val water = Color(0xFF38BDF8)
    val mains = Color(0xFF60A5FA)
    val gas = Color(0xFFFB923C)
    val heat = Color(0xFFFB923C)  // diesel heater, same orange as the touchscreen and web page
    val climate = Color(0xFFA78BFA)
    val warn = Color(0xFFF59E0B)
    val alarm = Color(0xFFEF4444)
    val secure = Color(0xFF84CC16)  // secure mode armed, same green as the touchscreen and web page
    val critical = Color(0xFFD946EF)  // fuchsia, same as the touchscreen and web page
    val muted = Color(0xFF9EA4A6)
}

/** Background and card colours (dark theme). */
object Surfaces {
    val background = Color(0xFF0E0F10)  // camo base
    val camo = listOf(Color(0xFF141617), Color(0xFF191C1C), Color(0xFF1F2321))  // camo layers
    val plate = Color(0xFF0B0C0D)       // chequer plate base (bottom bar)
    val ridge = Color(0xFF1B1D1F)       // chequer plate ridges
    val ridgeLight = Color(0xFF2A2D30)
    val card = Color(0xFF1E2124)        // gunmetal
    val raised = Color(0xFF2A2E32)
    val track = Color(0xFF34383C)
    val text = Color(0xFFECEEEA)
    val onLime = Color(0xFF16210A)
    val limeContainer = Color(0xFF34451A)  // tonal lime (selected chips, tab indicator)
}

/** Material colour scheme for the app (also used by the screenshot renderer). */
val CaravanColors = darkColorScheme(
    primary = Accent.lime,
    onPrimary = Surfaces.onLime,
    secondary = Accent.battery,
    onSecondary = Surfaces.onLime,
    tertiary = Accent.water,
    primaryContainer = Surfaces.limeContainer,
    onPrimaryContainer = Surfaces.text,
    secondaryContainer = Surfaces.limeContainer,
    onSecondaryContainer = Surfaces.text,
    background = Surfaces.background,
    surface = Surfaces.background,
    onSurface = Surfaces.text,
    onBackground = Surfaces.text,
    surfaceVariant = Surfaces.raised,
    onSurfaceVariant = Accent.muted,
    outline = Surfaces.track,
    outlineVariant = Surfaces.raised,
    error = Accent.alarm,
)

fun sevColor(sev: Int) = when {
    sev >= Severity.CRIT -> Accent.critical
    sev == Severity.ALARM -> Accent.alarm
    sev == Severity.WARN -> Accent.warn
    else -> Accent.lime
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
