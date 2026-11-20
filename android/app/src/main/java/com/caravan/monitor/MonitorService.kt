package com.caravan.monitor

import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

/**
 * Keeps watching the caravan with the app closed, so alerts still arrive:
 * - on the caravan's Wi-Fi, through the hub connection (no internet needed);
 * - away from the van, through ntfy (needs the van to have internet).
 */
class MonitorService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        val app = CaravanApp.instance
        goForeground(app.notifier.monitorNotification("Starting…"))
        app.hub.acquire()
        scope.launch { NtfyClient(app.prefs, app.hub, app.notifier).run() }
        scope.launch {
            combine(app.hub.status, app.hub.age, app.hub.state.map { it?.bat?.soc?.toInt() }) { st, age, soc ->
                when {
                    // Never show an old battery % as if it were current.
                    st.conn == Conn.CONNECTED && !age.live -> "No connection: " + age.why.replaceFirstChar { it.lowercase() }
                    st.conn == Conn.CONNECTED -> "Connected to the hub" + (soc?.let { " · battery $it%" } ?: "")
                    else -> if (app.prefs.settings.value.ntfyTopic.isNotBlank())
                        "Away from the van: alerts come through ntfy" else "Not connected to the hub"
                }
            }.distinctUntilChanged().collectLatest { text ->
                try {
                    NotificationManagerCompat.from(this@MonitorService)
                        .notify(AlertNotifier.NOTIF_MONITOR, app.notifier.monitorNotification(text))
                } catch (_: SecurityException) {}
            }
        }
    }

    private fun goForeground(n: android.app.Notification) {
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(AlertNotifier.NOTIF_MONITOR, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(AlertNotifier.NOTIF_MONITOR, n)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_STICKY

    override fun onDestroy() {
        scope.cancel()
        CaravanApp.instance.hub.release()
        super.onDestroy()
    }

    companion object {
        /** Start or stop the service to match the "alerts in the background" setting. */
        fun sync(context: Context) {
            val on = CaravanApp.instance.prefs.settings.value.backgroundAlerts
            val i = Intent(context, MonitorService::class.java)
            if (on) {
                try { ContextCompat.startForegroundService(context, i) } catch (_: Exception) {
                    // Android may refuse a start from the background; the app retries when opened.
                }
            } else {
                context.stopService(i)
            }
        }
    }
}
