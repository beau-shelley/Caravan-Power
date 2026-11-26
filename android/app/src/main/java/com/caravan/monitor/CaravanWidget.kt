package com.caravan.monitor

import android.app.AlarmManager
import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.SizeF
import android.view.View
import android.widget.RemoteViews
import com.caravan.monitor.ui.MainActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

/**
 * Home-screen widget with the caravan's vitals. It keeps no connection of its own (no extra
 * battery use): the app pushes a new view while it has live data, at most every 30 s
 * ([WidgetFeed]). Tap it to open the app.
 */
class CaravanWidget : AppWidgetProvider() {
    override fun onUpdate(context: Context, manager: AppWidgetManager, ids: IntArray) {
        renderAll(context)
        try { CaravanApp.instance.widget.widgetAdded() } catch (_: Exception) {}
    }

    override fun onAppWidgetOptionsChanged(context: Context, manager: AppWidgetManager, id: Int, options: Bundle) =
        renderAll(context)

    override fun onReceive(context: Context, intent: Intent) {
        super.onReceive(context, intent)
        // Set when a live view was drawn: if the app hasn't refreshed it by then, it isn't live any more.
        if (intent.action == ACTION_STALE) renderAll(context)
    }

    companion object {
        const val ACTION_STALE = "com.caravan.monitor.WIDGET_STALE"
        private const val PREFS = "widget"

        fun ids(context: Context): IntArray = try {
            AppWidgetManager.getInstance(context).getAppWidgetIds(ComponentName(context, CaravanWidget::class.java))
        } catch (_: Exception) { IntArray(0) }

        /** Save [v] and draw it on every widget. */
        fun push(context: Context, v: WidgetView) {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("v", v.toJson()).putLong("at", System.currentTimeMillis()).apply()
            renderAll(context, v)
            scheduleStaleCheck(context, v.live)
        }

        /** Draw the saved view (blanked if it's too old to be live). */
        fun renderAll(context: Context, fresh: WidgetView? = null) {
            val ids = ids(context)
            if (ids.isEmpty()) return
            val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val now = System.currentTimeMillis()
            val v = fresh ?: (WidgetView.fromJson(p.getString("v", null)) ?: WidgetView()).at(now, p.getLong("at", 0))
            val manager = AppWidgetManager.getInstance(context)
            for (id in ids) {
                try { manager.updateAppWidget(id, views(context, v)) } catch (_: Exception) {}
            }
        }

        private fun views(context: Context, v: WidgetView): RemoteViews {
            val full = full(context, v)
            val small = small(context, v)
            return if (Build.VERSION.SDK_INT >= 31) {
                // The launcher picks the layout that fits the widget's size.
                RemoteViews(mapOf(SizeF(110f, 40f) to small, SizeF(220f, 100f) to full))
            } else full
        }

        private fun full(context: Context, v: WidgetView) = RemoteViews(context.packageName, R.layout.widget_caravan).apply {
            setOnClickPendingIntent(R.id.w_root, openApp(context))
            if (v.alerts.isNotEmpty()) {
                setTextViewText(R.id.w_title, "⚠ " + v.alerts)
                setTextColor(R.id.w_title, v.alertColor.toInt())
            } else {
                setTextViewText(R.id.w_title, "CARAVAN")
                setTextColor(R.id.w_title, WidgetColors.LIME.toInt())
            }
            setTextViewText(R.id.w_status, v.status)
            setTextColor(R.id.w_status, (if (v.live) WidgetColors.MUTED else WidgetColors.WARN).toInt())
            setTextViewText(R.id.w_soc, v.soc)
            setTextColor(R.id.w_soc, v.socColor.toInt())
            setTextViewText(R.id.w_flow, v.flow)
            setTextColor(R.id.w_flow, v.flowColor.toInt())
            setTextViewText(R.id.w_time, v.time)
            setViewVisibility(R.id.w_time, if (v.time.isEmpty()) View.GONE else View.VISIBLE)
            bar(this, v)
            tile(this, R.id.w_water_l, R.id.w_water_v, v.water, WidgetColors.WATER)
            tile(this, R.id.w_gas_l, R.id.w_gas_v, v.gas, WidgetColors.GAS)
            tile(this, R.id.w_in_l, R.id.w_in_v, v.inside, WidgetColors.TEXT)
            tile(this, R.id.w_fr_l, R.id.w_fr_v, v.fridge, WidgetColors.TEXT)
        }

        private fun small(context: Context, v: WidgetView) = RemoteViews(context.packageName, R.layout.widget_caravan_small).apply {
            setOnClickPendingIntent(R.id.w_root, openApp(context))
            setTextViewText(R.id.w_soc, v.soc)
            setTextColor(R.id.w_soc, v.socColor.toInt())
            setTextViewText(R.id.w_status, if (v.alerts.isNotEmpty()) "⚠ " + v.alerts else v.statusShort)
            setTextColor(R.id.w_status, when {
                v.alerts.isNotEmpty() -> v.alertColor
                v.live -> WidgetColors.MUTED
                else -> WidgetColors.WARN
            }.toInt())
            bar(this, v)
        }

        /** One bar per colour (RemoteViews can't tint a bar before Android 12); only the right one shows. */
        private fun bar(rv: RemoteViews, v: WidgetView) {
            val show = when {
                !v.live -> R.id.w_bar_ok
                v.socColor == WidgetColors.ALARM -> R.id.w_bar_low
                v.socColor == WidgetColors.WARN -> R.id.w_bar_warn
                else -> R.id.w_bar_ok
            }
            for (id in intArrayOf(R.id.w_bar_ok, R.id.w_bar_warn, R.id.w_bar_low)) {
                rv.setViewVisibility(id, if (id == show) View.VISIBLE else View.GONE)
                rv.setProgressBar(id, 100, if (id == show) v.socPct else 0, false)
            }
        }

        private fun tile(rv: RemoteViews, label: Int, value: Int, t: WidgetTile, labelColor: Long) {
            rv.setTextViewText(label, t.label)
            rv.setTextColor(label, (if (t.value == "--") WidgetColors.MUTED else labelColor).toInt())
            rv.setTextViewText(value, t.value)
            rv.setTextColor(value, t.color.toInt())
        }

        private fun openApp(context: Context): PendingIntent = PendingIntent.getActivity(
            context, 40, Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        /**
         * After drawing a live view, check again in 90 s: if the app was stopped and couldn't
         * refresh it, the widget blanks itself. Not a wake-up alarm: it runs when the phone is
         * next awake, which is before anyone can look at the widget.
         */
        private fun scheduleStaleCheck(context: Context, live: Boolean) {
            val am = context.getSystemService(AlarmManager::class.java) ?: return
            val pi = PendingIntent.getBroadcast(
                context, 41, Intent(context, CaravanWidget::class.java).setAction(ACTION_STALE),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
            try {
                if (live) am.set(AlarmManager.RTC, System.currentTimeMillis() + WidgetModel.STALE_MS + 1_000, pi)
                else am.cancel(pi)
            } catch (_: Exception) {}
        }
    }
}

/** Feeds the widget from the app's hub connection. Does nothing while no widget is on the home screen. */
class WidgetFeed(private val context: Context, private val hub: HubClient, private val usage: UsageTracker) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var last: WidgetView? = null
    private var lastPushAt = 0L
    @Volatile private var idsCheckedAt = 0L
    private var hasWidgets = false

    fun start() {
        scope.launch {
            combine(hub.state, hub.age, hub.meta, hub.status, usage.outlook) { st, age, meta, conn, outlook ->
                val now = System.currentTimeMillis()
                WidgetModel.build(st, meta, age, outlook, conn, now, last) to now
            }.collect { (v, now) ->
                // Re-check now and then whether any widget is placed (cheap, but not every second).
                if (now - idsCheckedAt > 60_000 || now < idsCheckedAt) {
                    idsCheckedAt = now
                    val had = hasWidgets
                    hasWidgets = CaravanWidget.ids(context).isNotEmpty()
                    if (hasWidgets && !had) last = null
                }
                if (!hasWidgets) return@collect
                if (WidgetModel.shouldPush(v, last, lastPushAt, now)) {
                    last = v
                    lastPushAt = now
                    CaravanWidget.push(context, v)
                }
            }
        }
    }

    /** A widget was just placed: draw it now instead of waiting for the next check. */
    fun widgetAdded() { idsCheckedAt = 0 }
}
