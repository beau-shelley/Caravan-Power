package com.caravan.monitor

/**
 * Works out which phone notifications to show or remove as the hub's list of
 * active alerts changes. Pure logic so it can be unit tested.
 *
 * An alert is shown once when it appears unacknowledged, and removed when it
 * clears or someone acknowledges it (on the phone, the touchscreen or the web page).
 */
class AlertTracker {
    data class Diff(val show: List<ActiveAlert>, val remove: List<Int>)

    private val shown = mutableSetOf<Int>()

    /** [muted] = rule ids whose "notify phone" is switched off. */
    @Synchronized
    fun update(alerts: List<ActiveAlert>, muted: Set<Int> = emptySet()): Diff {
        val wanted = alerts.filter { !it.ack && it.id !in muted }.associateBy { it.id }
        val show = wanted.values.filter { it.id !in shown }
        val remove = shown.filter { it !in wanted }
        shown.clear()
        shown.addAll(wanted.keys)
        return Diff(show, remove)
    }
}
