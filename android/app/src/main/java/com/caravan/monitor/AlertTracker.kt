package com.caravan.monitor

/**
 * Works out which phone notifications to show or remove as the hub's list of
 * active alerts changes. Pure logic so it can be unit tested.
 *
 * An alert is shown once when it appears unacknowledged, shown again if a
 * stepped rule escalates (e.g. warning at 30 % becomes critical at 5 %), and
 * removed when it clears or someone acknowledges it (on the phone, the
 * touchscreen or the web page).
 */
class AlertTracker {
    data class Diff(val show: List<ActiveAlert>, val remove: List<Int>)

    private val shown = mutableMapOf<Int, Int>()   // rule id -> current severity

    /** [muted] = rule ids whose "notify phone" is switched off. */
    @Synchronized
    fun update(alerts: List<ActiveAlert>, muted: Set<Int> = emptySet()): Diff {
        val wanted = alerts.filter { !it.ack && it.id !in muted }.associateBy { it.id }
        val show = wanted.values.filter { a -> shown[a.id].let { it == null || a.sev > it } }
        val remove = shown.keys.filter { it !in wanted }
        shown.clear()
        wanted.values.forEach { shown[it.id] = it.sev }   // a silent step back down re-arms the step
        return Diff(show, remove)
    }
}
