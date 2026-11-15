package com.caravan.monitor

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.caravan.monitor.ui.MainActivity

/** Turns hub alerts into Android notifications. */
class AlertNotifier(private val context: Context) {
    private val tracker = AlertTracker()
    private val nm = NotificationManagerCompat.from(context)

    init { createChannels() }

    private fun createChannels() {
        val m = context.getSystemService(NotificationManager::class.java)
        m.createNotificationChannels(listOf(
            NotificationChannel(CH_ALARM, "Alarms", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "Alarm-level alerts from the caravan, e.g. battery flat"
            },
            NotificationChannel(CH_WARN, "Warnings", NotificationManager.IMPORTANCE_HIGH).apply {
                description = "Warning-level alerts from the caravan"
            },
            NotificationChannel(CH_INFO, "Info", NotificationManager.IMPORTANCE_DEFAULT).apply {
                description = "Information alerts from the caravan"
            },
            NotificationChannel(CH_MONITOR, "Background connection", NotificationManager.IMPORTANCE_MIN).apply {
                description = "Shown while the app watches the caravan for alerts"
                setShowBadge(false)
            },
        ))
    }

    /** Called with every state message from the hub on the local Wi-Fi. */
    fun onLocalState(state: HubState, rules: List<Rule>, meta: Meta) {
        val muted = rules.filter { !it.notify }.map { it.id }.toSet()
        val diff = tracker.update(state.alerts, muted)
        diff.remove.forEach { nm.cancel(NOTIF_BASE + it) }
        diff.show.forEach { a ->
            val title = rules.firstOrNull { it.id == a.id }?.name?.takeIf { it.isNotBlank() } ?: Severity.label(a.sev)
            post(NOTIF_BASE + a.id, title, a.msg, a.sev, withAck = true)
        }
    }

    /**
     * An alert that came through ntfy while away from the van. The hub sends a
     * "Cleared: <name>" message when it clears, which replaces the original.
     */
    fun onRemote(title: String, message: String, priority: Int) {
        val cleared = title.startsWith("Cleared: ")
        val name = title.removePrefix("Cleared: ")
        val id = REMOTE_BASE + (name.hashCode() and 0xffff)
        val sev = when { cleared -> Severity.INFO; priority >= 5 -> Severity.ALARM; priority >= 4 -> Severity.WARN; else -> Severity.INFO }
        post(id, title, message, sev, withAck = false)
    }

    private fun post(id: Int, title: String, text: String, sev: Int, withAck: Boolean) {
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return
        val open = PendingIntent.getActivity(
            context, 0, Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val channel = when (sev) { Severity.ALARM -> CH_ALARM; Severity.WARN -> CH_WARN; else -> CH_INFO }
        val b = NotificationCompat.Builder(context, channel)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(open)
            .setAutoCancel(true)
            .setCategory(if (sev == Severity.ALARM) NotificationCompat.CATEGORY_ALARM else NotificationCompat.CATEGORY_STATUS)
            .setPriority(if (sev >= Severity.WARN) NotificationCompat.PRIORITY_HIGH else NotificationCompat.PRIORITY_DEFAULT)
        if (withAck) {
            val ack = PendingIntent.getBroadcast(
                context, 1, Intent(context, AckReceiver::class.java),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
            b.addAction(0, "Acknowledge", ack)
        }
        try { nm.notify(id, b.build()) } catch (_: SecurityException) {}
    }

    fun monitorNotification(text: String) =
        NotificationCompat.Builder(context, CH_MONITOR)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("Watching caravan")
            .setContentText(text)
            .setOngoing(true)
            .setSilent(true)
            .setContentIntent(PendingIntent.getActivity(
                context, 2, Intent(context, MainActivity::class.java),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            ))
            .build()

    companion object {
        const val CH_ALARM = "alarm"
        const val CH_WARN = "warn"
        const val CH_INFO = "info"
        const val CH_MONITOR = "monitor"
        const val NOTIF_MONITOR = 1
        private const val NOTIF_BASE = 1000
        private const val REMOTE_BASE = 100_000
    }
}
