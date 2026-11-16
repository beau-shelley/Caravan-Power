package com.caravan.monitor

import org.junit.Assert.assertEquals
import org.junit.Test

class AlertTrackerTest {
    private fun a(id: Int, ack: Boolean = false) = ActiveAlert(id, 1, ack, 0, 0.0, "msg $id")

    @Test fun showsNewAlertOnce() {
        val t = AlertTracker()
        assertEquals(listOf(1), t.update(listOf(a(1))).show.map { it.id })
        assertEquals(emptyList<Int>(), t.update(listOf(a(1))).show.map { it.id })
    }

    @Test fun removesWhenClearedOrAcked() {
        val t = AlertTracker()
        t.update(listOf(a(1), a(2)))
        val d = t.update(listOf(a(1, ack = true)))
        assertEquals(setOf(1, 2), d.remove.toSet())
        assertEquals(emptyList<ActiveAlert>(), d.show)
    }

    @Test fun mutedRulesAreSkipped() {
        val t = AlertTracker()
        assertEquals(listOf(2), t.update(listOf(a(1), a(2)), muted = setOf(1)).show.map { it.id })
    }

    @Test fun reRaisedAlertShowsAgain() {
        val t = AlertTracker()
        t.update(listOf(a(1)))
        t.update(emptyList())
        assertEquals(listOf(1), t.update(listOf(a(1))).show.map { it.id })
    }

    @Test fun escalationShowsAgainButNotOnTheWayDown() {
        val t = AlertTracker()
        fun s(sev: Int) = ActiveAlert(4, sev, false, 0, 0.0, "charge")
        assertEquals(1, t.update(listOf(s(Severity.WARN))).show.size)
        assertEquals(1, t.update(listOf(s(Severity.ALARM))).show.size)
        assertEquals(1, t.update(listOf(s(Severity.CRIT))).show.size)
        assertEquals(0, t.update(listOf(s(Severity.ALARM))).show.size)   // recovered a step: silent
        assertEquals(1, t.update(listOf(s(Severity.CRIT))).show.size)    // fell again: alert again
    }
}
