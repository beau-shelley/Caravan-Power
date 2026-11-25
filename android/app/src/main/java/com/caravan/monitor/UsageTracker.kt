package com.caravan.monitor

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.io.File

/**
 * Phone-side extras (no work for the hub): battery time at the current load and gas days
 * left. Fed with every live state by [HubClient].
 */
class UsageTracker(context: Context) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val file = File(context.filesDir, "usage.csv")
    private val log = UsageLog()
    private val lock = Any()
    private val _outlook = MutableStateFlow(Outlook())
    /** Shown on Home; empty while the data isn't live. */
    val outlook: StateFlow<Outlook> = _outlook

    init {
        scope.launch {
            try { if (file.exists()) synchronized(lock) { log.load(file.readLines()) } } catch (e: Exception) { Log.w(TAG, "load", e) }
        }
    }

    fun onLive(st: HubState, m: Meta) {
        val now = System.currentTimeMillis()
        val done = synchronized(lock) {
            val d = log.add(st, now)
            _outlook.value = log.outlook(st, m, now / 1000)
            d
        }
        if (done) scope.launch { save() }
    }

    /** The data stopped being live: show nothing rather than old numbers. */
    fun onNotLive() {
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

    companion object { private const val TAG = "Usage" }
}
