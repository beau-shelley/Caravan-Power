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

private fun c(v: Long) = Color(v.toInt())
private val BG = Color(0xF2111214)
private val STROKE = Color(0x809BE03A)
private val TILE = Color(0xFF4E5257)
private val TRACK = Color(0xFF36393D)

@Composable
private fun WBar(v: WidgetView) {
    Box(Modifier.fillMaxWidth().padding(top = 4.dp).height(6.dp).clip(RoundedCornerShape(3.dp)).background(TRACK)) {
        if (v.live && v.socPct > 0) Box(Modifier.fillMaxWidth(v.socPct / 100f).fillMaxHeight().clip(RoundedCornerShape(3.dp)).background(c(v.socColor)))
    }
}

@Composable
private fun WTile(t: WidgetTile, labelColor: Long, modifier: Modifier) {
    Column(modifier.fillMaxHeight().clip(RoundedCornerShape(10.dp)).background(TILE).padding(start = 8.dp, end = 4.dp, top = 4.dp, bottom = 4.dp),
        verticalArrangement = Arrangement.Center) {
        Text(t.label, color = c(if (t.value == "--") WidgetColors.MUTED else labelColor), fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(t.value, color = c(t.color), fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 1)
    }
}

@Composable
fun WidgetFull(v: WidgetView, w: Int = 380, h: Int = 170) {
    Column(Modifier.size(w.dp, h.dp).clip(RoundedCornerShape(18.dp)).background(BG).border(1.dp, STROKE, RoundedCornerShape(18.dp))
        .padding(horizontal = 14.dp, vertical = 10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (v.alerts.isNotEmpty()) Text("⚠ " + v.alerts, color = c(v.alertColor), fontSize = 12.sp, fontWeight = FontWeight.Bold)
            else Text("CARAVAN", color = c(WidgetColors.LIME), fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            Text(v.status, Modifier.weight(1f).padding(start = 8.dp), color = c(if (v.live) WidgetColors.MUTED else WidgetColors.WARN),
                fontSize = 11.sp, textAlign = TextAlign.End, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Row(Modifier.fillMaxWidth().weight(1f), verticalAlignment = Alignment.CenterVertically) {
            Text(v.soc, color = c(v.socColor), fontSize = 30.sp, fontWeight = FontWeight.Bold)
            Column(Modifier.weight(1f).padding(start = 12.dp)) {
                Text(v.flow, color = c(if (v.live) WidgetColors.TEXT else WidgetColors.MUTED), fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (v.time.isNotEmpty()) Text(v.time, color = Color(0xFFD3D6D9), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        WBar(v)
        Row(Modifier.fillMaxWidth().padding(top = 8.dp).height(54.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            WTile(v.water, WidgetColors.WATER, Modifier.weight(1f))
            WTile(v.gas, WidgetColors.GAS, Modifier.weight(1f))
            WTile(v.inside, WidgetColors.TEXT, Modifier.weight(1f))
            WTile(v.fridge, WidgetColors.TEXT, Modifier.weight(1f))
        }
    }
}

@Composable
fun WidgetSmall(v: WidgetView, w: Int = 180, h: Int = 64) {
    Column(Modifier.size(w.dp, h.dp).clip(RoundedCornerShape(18.dp)).background(BG).border(1.dp, STROKE, RoundedCornerShape(18.dp))
        .padding(horizontal = 12.dp, vertical = 6.dp), verticalArrangement = Arrangement.Center) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(v.soc, color = c(v.socColor), fontSize = 24.sp, fontWeight = FontWeight.Bold)
            val s = if (v.alerts.isNotEmpty()) "⚠ " + v.alerts else v.statusShort
            val col = when { v.alerts.isNotEmpty() -> v.alertColor; v.live -> WidgetColors.MUTED; else -> WidgetColors.WARN }
            Text(s, Modifier.weight(1f).padding(start = 8.dp), color = c(col), fontSize = 11.sp, textAlign = TextAlign.End, maxLines = 2)
        }
        WBar(v)
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
    val vEvening = WidgetModel.build(evening, META, live, Outlook(battery = "About 2½ days left at this load"), CONNECTED, now)
    val vCharging = WidgetModel.build(CHARGING, META, live, Outlook(battery = "Full in 34 min"), CONNECTED, now)
    val vAlert = WidgetModel.build(ALERTING, META, live, Outlook(battery = "45 min left at this load"), CONNECTED.copy(cloud = true), now)
    val vStale = vEvening.blanked(now - 47 * 60_000, now)
    val vNever = WidgetModel.build(null, Meta(), DataAge(live = false), Outlook(), ConnStatus(), now)

    shot("widget-1-live", 220, 412) { Wallpaper { WidgetFull(vEvening) } }
    shot("widget-2-charging", 220, 412) { Wallpaper { WidgetFull(vCharging) } }
    shot("widget-3-alerts", 220, 412) { Wallpaper { WidgetFull(vAlert) } }
    shot("widget-4-no-connection", 220, 412) { Wallpaper { WidgetFull(vStale) } }
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
