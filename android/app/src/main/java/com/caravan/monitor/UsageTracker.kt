package com.caravan.monitor

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.io.File
import java.util.Calendar

/**
 * Phone-side extras (no work for the hub): battery time at the current load, gas days left,
 * and the morning summary notification. Fed with every live state by [HubClient].
 */
class UsageTracker(
    private val context: Context,
    private val prefs: Prefs,
    private val notifier: AlertNotifier,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val file = File(context.filesDir, "usage.csv")
    private val log = UsageLog()
    private val lock = Any()
    private val _outlook = MutableStateFlow(Outlook())
    /** Shown on Home; empty while the data isn't live. */
    val outlook: StateFlow<Outlook> = _outlook
    @Volatile private var lastLive: HubState? = null
    @Volatile private var meta = Meta()

    init {
        scope.launch {
            try { if (file.exists()) synchronized(lock) { log.load(file.readLines()) } } catch (e: Exception) { Log.w(TAG, "load", e) }
            // Checks once a minute whether the morning summary is due.
            while (isActive) { delay(60_000); try { maybeSummary() } catch (e: Exception) { Log.w(TAG, "summary", e) } }
        }
    }

    fun onLive(st: HubState, m: Meta) {
        meta = m
        lastLive = st
        val now = System.currentTimeMillis()
        val done = synchronized(lock) {
            val d = log.add(st, now)
            _outlook.value = log.outlook(st, m, now / 1000)
            d
        }
        if (done != null) scope.launch { save() }
    }

    /** The data stopped being live: show nothing rather than old numbers. */
    fun onNotLive() {
        lastLive = null
        synchronized(lock) { log.lost() }
        if (_outlook.value != Outlook()) _outlook.value = Outlook()
    }

    private fun save() {
        try {
            val lines = synchronized(lock) { log.lines() }
            val tmp = File(file.path + ".tmp")
            tmp.writeText(lines.joinToString("\n"))
            tmp.renameTo(file)
        } catch (e: Exception) { Log.w(TAG, "save", e) }
    }

    private fun maybeSummary() {
        val s = prefs.settings.value
        if (!s.morningSummary) return
        val cal = Calendar.getInstance()
        val hour = cal.get(Calendar.HOUR_OF_DAY)
        val today = cal.get(Calendar.YEAR) * 1000 + cal.get(Calendar.DAY_OF_YEAR)
        // 7 am, or as soon after as the phone is awake; not after noon (it'd no longer be a morning summary).
        if (hour < SUMMARY_HOUR || hour >= 12 || prefs.lastSummaryDay == today) return
        prefs.lastSummaryDay = today
        val nowS = System.currentTimeMillis() / 1000
        val text = synchronized(lock) {
            UsageLog.summary(log.all(), nowS, ::localMidnight, meta, lastLive, log.avgA)
        } ?: return  // the app heard nothing from the van in the last day
        notifier.summary(text)
    }

    companion object {
        private const val TAG = "Usage"
        const val SUMMARY_HOUR = 7

        fun localMidnight(t: Long): Long = Calendar.getInstance().apply {
            timeInMillis = t * 1000
            set(Calendar.HOUR_OF_DAY, 0); set(Calendar.MINUTE, 0); set(Calendar.SECOND, 0); set(Calendar.MILLISECOND, 0)
        }.timeInMillis / 1000
    }
}
