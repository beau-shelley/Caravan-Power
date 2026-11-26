// Draws the home-screen widget (app/src/main/res/layout/widget_caravan*.xml) from the
// same WidgetModel the app uses, on a plain "wallpaper". Sizes and colours copy the XML.

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.material3.ProvideTextStyle
import androidx.compose.ui.text.TextStyle
import com.caravan.monitor.*
import androidx.compose.foundation.Canvas
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.lerp

private fun c(v: Long) = Color(v.toInt())
private val BG = Brush.linearGradient(listOf(Color(0xF52C3035), Color(0xF8101214)))
private val EDGE = Color(0x1FFFFFFF)

/** Mirrors WidgetRing.kt (bitmap drawn with android.graphics in the app). */
@Composable
private fun WRing(v: WidgetView, size: Int) {
    Canvas(Modifier.size(size.dp)) {
        val sw = this.size.width * (if (size >= 70) 0.10f else 0.12f)
        val tl = Offset(sw / 2 + 1, sw / 2 + 1)
        val arc = Size(this.size.width - sw - 2, this.size.height - sw - 2)
        if (v.live) drawArc(Color(0xFF0D0E10), 0f, 360f, false, tl, arc, style = Stroke(sw))
        else drawArc(Color(0xFF2E3135), 0f, 360f, false, tl, arc,
            style = Stroke(sw, pathEffect = PathEffect.dashPathEffect(floatArrayOf(sw * 0.45f, sw * 0.65f))))
        if (v.live && v.socPct > 0) {
            val f = (v.socPct / 100f).coerceIn(0.01f, 1f)
            val base = c(v.socColor)
            val deep = lerp(base, Color.Black, 0.3f); val light = lerp(base, Color.White, 0.45f)
            val brush = Brush.sweepGradient(0f to deep, f * 0.55f to base, f to light, 1f to light, center = center)
            rotate(-90f) { drawArc(brush, 0f, 360f * f, false, tl, arc, style = Stroke(sw, cap = StrokeCap.Round)) }
            drawCircle(deep, sw / 2, Offset(center.x, tl.y))
        }
        if (size >= 70 && v.live && v.netFrac != 0.0) {
            val nw = sw * 0.38f
            val inset = sw + nw * 1.6f
            val itl = Offset(inset, inset)
            val isz = Size(this.size.width - 2 * inset, this.size.height - 2 * inset)
            drawArc(Color(0xFF1A1C1F), 0f, 360f, false, itl, isz, style = Stroke(nw))
            drawArc(c(v.netColor), -90f, (360.0 * v.netFrac).toFloat(), false, itl, isz, style = Stroke(nw, cap = StrokeCap.Round))
        }
    }
}

private fun bgFor(v: WidgetView): Pair<Brush, Pair<Color, Float>> = when {
    v.alerts.isEmpty() -> BG to (EDGE to 1f)
    v.alertColor == WidgetColors.WARN -> Brush.linearGradient(listOf(Color(0xF5463A22), Color(0xF8121214))) to (Color(0xB3FFB020) to 1.5f)
    else -> Brush.linearGradient(listOf(Color(0xF5461E28), Color(0xF8121014))) to (Color(0xB3FF6B6B) to 1.5f)
}

@Composable
private fun WTile(t: WidgetTile, labelColor: Long, modifier: Modifier) {
    Column(modifier.fillMaxHeight().clip(RoundedCornerShape(14.dp))
        .background(Brush.verticalGradient(listOf(Color(0x14FFFFFF), Color(0x0AFFFFFF))))
        .border(1.dp, Color(0x0DFFFFFF), RoundedCornerShape(14.dp))
        .padding(start = 10.dp, end = 4.dp, top = 3.dp, bottom = 3.dp),
        verticalArrangement = Arrangement.Center) {
        Text(t.label, color = c(if (t.value == "--") WidgetColors.MUTED else labelColor), fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(t.value, color = c(t.color), fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
fun WidgetFull(v: WidgetView, w: Int = 380, h: Int = 188) {
    val (bg, edge) = bgFor(v)
    Column(Modifier.size(w.dp, h.dp).clip(RoundedCornerShape(24.dp)).background(bg).border(edge.second.dp, edge.first, RoundedCornerShape(24.dp))
        .padding(horizontal = 14.dp, vertical = 12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (v.alerts.isNotEmpty()) Text("⚠ " + v.alerts, color = c(v.alertColor), fontSize = 12.sp, fontWeight = FontWeight.Bold)
            else Text("CARAVAN", color = c(WidgetColors.LIME), fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.4.sp)
            Text(v.status, Modifier.weight(1f).padding(start = 8.dp), color = c(if (v.live) WidgetColors.MUTED else WidgetColors.WARN),
                fontSize = 11.sp, textAlign = TextAlign.End, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Row(Modifier.fillMaxWidth().weight(1f).padding(top = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(contentAlignment = Alignment.Center) {
                WRing(v, 84)
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(v.soc, color = c(v.socColor), fontSize = 18.sp, fontWeight = FontWeight.Bold, lineHeight = 19.sp)
                    Text(v.net, color = c(v.netColor), fontSize = 10.sp, fontWeight = FontWeight.Bold, lineHeight = 11.sp)
                }
            }
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Text("Net, 1 min average", color = c(WidgetColors.MUTED), fontSize = 11.sp, maxLines = 1)
                Text(v.flow, color = c(v.flowColor), fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (v.time.isNotEmpty()) Text(v.time, color = Color(0xFFD3D6D9), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp).height(50.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            WTile(v.water, WidgetColors.WATER, Modifier.weight(1f))
            WTile(v.gas, WidgetColors.GAS, Modifier.weight(1f))
            WTile(v.inside, WidgetColors.TEXT, Modifier.weight(1f))
            WTile(v.fridge, WidgetColors.TEXT, Modifier.weight(1f))
        }
    }
}

@Composable
fun WidgetSmall(v: WidgetView, w: Int = 180, h: Int = 64) {
    Row(Modifier.size(w.dp, h.dp).clip(RoundedCornerShape(24.dp)).background(BG).border(1.dp, EDGE, RoundedCornerShape(24.dp))
        .padding(horizontal = 12.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        WRing(v, 40)
        Column(Modifier.weight(1f).padding(start = 10.dp)) {
            Text(v.soc, color = c(v.socColor), fontSize = 22.sp, fontWeight = FontWeight.Bold)
            val s = if (v.alerts.isNotEmpty()) "⚠ " + v.alerts else v.statusShort
            val col = when { v.alerts.isNotEmpty() -> v.alertColor; v.live -> WidgetColors.MUTED; else -> WidgetColors.WARN }
            Text(s, color = c(col), fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}

@Composable
private fun Wallpaper(content: @Composable () -> Unit) = ProvideTextStyle(TextStyle(fontFamily = FONT)) {
    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color(0xFF2B3A4A), Color(0xFF5A6B52), Color(0xFF3A3328)))),
        contentAlignment = Alignment.Center) { content() }
}

fun widgetShots() {
    val now = System.currentTimeMillis()
    val live = DataAge(live = true, lastAt = now)
    // Evening off-grid: running on the battery.
    val evening = CHARGING.copy(
        bat = Battery(on = true, v = 13.18, a = -6.4, soc = 78.0, ttgMin = 2800, solarA = 0.0, chargerA = 0.0, loadA = 6.4),
        tmp = listOf(Temp(true, 21.4, 17.2, 24.1, 80), Temp(true, 14.2, 11.0, 26.0, 75), Temp(true, 3.4, 2.8, 4.6, 90)),
        tank = listOf(Tank(true, 72.0), Tank(true, 45.0), Tank(true, 30.0)),
    )
    val vEvening = WidgetModel.build(evening, META, live, Outlook(battery = "About 2½ days left at this load"), CONNECTED, now, netAvgA = -8.1)
    val vCharging = WidgetModel.build(CHARGING, META, live, Outlook(battery = "Full in 34 min"), CONNECTED, now, netAvgA = 31.6)
    val vAlert = WidgetModel.build(ALERTING, META, live, Outlook(battery = "45 min left at this load"), CONNECTED.copy(cloud = true), now)
    val vStale = vEvening.blanked(now - 47 * 60_000, now)
    val vNever = WidgetModel.build(null, Meta(), DataAge(live = false), Outlook(), ConnStatus(), now)

    shot("widget-1-live", 230, 412) { Wallpaper { WidgetFull(vEvening) } }
    shot("widget-2-charging", 230, 412) { Wallpaper { WidgetFull(vCharging) } }
    shot("widget-3-alerts", 230, 412) { Wallpaper { WidgetFull(vAlert) } }
    shot("widget-4-no-connection", 230, 412) { Wallpaper { WidgetFull(vStale) } }
    shot("widget-5-small", 130, 412) {
        Wallpaper { Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) { WidgetSmall(vEvening); WidgetSmall(vStale) } }
    }
    shot("widget-all", 980, 412) {
        Wallpaper {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(14.dp)) {
                WidgetFull(vEvening); WidgetFull(vCharging); WidgetFull(vAlert); WidgetFull(vStale)
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) { WidgetSmall(vEvening); WidgetSmall(vNever) }
            }
        }
    }
}
