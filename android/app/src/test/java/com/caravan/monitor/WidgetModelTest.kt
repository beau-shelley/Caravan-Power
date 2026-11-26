package com.caravan.monitor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class WidgetModelTest {
    private val now = 1_764_000_000_000L
    private val meta = Meta(tankNames = listOf("Fresh 1", "Fresh 2", "Grey"))
    private val live = DataAge(live = true, lastAt = now)
    private val conn = ConnStatus(Conn.CONNECTED, "caravan.local")

    private fun state(a: Double = -4.2, soc: Double = 78.4, alerts: List<ActiveAlert> = emptyList()) = HubState(
        ts = now / 1000,
        bat = Battery(on = true, v = 13.2, a = a, soc = soc, ttgMin = 600, solarA = 0.0, chargerA = 0.0, loadA = -a),
        sol = emptyList(),
        tmp = listOf(Temp(true, 21.6, 0.0, 0.0, -1), Temp(true, 14.0, 0.0, 0.0, -1), Temp(true, 3.44, 0.0, 0.0, -1)),
        tank = listOf(Tank(true, 64.6), Tank(true, 30.0), Tank(false, 0.0)),
        pump = emptyList(), alerts = alerts,
        wx = Weather(false, "", 0.0, 0.0, 0.0, -1), net = Net(true, true, ""),
        gas = Gas(true, 42.0),
    )

    @Test fun liveValues() {
        val v = WidgetModel.build(state(), meta, live, Outlook(battery = "About 2 days left at this load"), conn, now)
        assertTrue(v.live)
        assertEquals("78%", v.soc)
        assertEquals(78, v.socPct)
        assertEquals("Net -4.2 A · using", v.flow)
        assertEquals(WidgetColors.WARN, v.flowColor)
        assertEquals("About 2 days left at this load", v.time)
        assertEquals(WidgetTile("Fresh 1", "65%", WidgetColors.WATER), v.water)
        assertEquals("42%", v.gas.value)
        assertEquals("22°", v.inside.value)   // whole degrees
        assertEquals("3.4°", v.fridge.value)  // fridge to 1 decimal
        assertTrue(v.status.startsWith("Live · "))
        assertEquals("", v.alerts)
    }

    @Test fun chargingAndAlerts() {
        val al = listOf(ActiveAlert(1, Severity.WARN, false, 0, 0.0, ""), ActiveAlert(2, Severity.ALARM, false, 0, 0.0, ""),
            ActiveAlert(3, Severity.CRIT, true, 0, 0.0, ""))
        val v = WidgetModel.build(state(a = 12.34, alerts = al), meta, live, Outlook(battery = "Charging"), conn.copy(cloud = true), now)
        assertEquals("Net +12.3 A · charging", v.flow)
        assertEquals("", v.time)  // "Charging" would only repeat the line above
        assertEquals("2 alerts", v.alerts)
        assertEquals(WidgetColors.ALARM, v.alertColor)  // the acknowledged critical one doesn't count
        assertTrue(v.status.startsWith("Live via cloud"))
    }

    @Test fun notLiveShowsNoNumbers() {
        val lastAt = now - 5 * 60_000
        val v = WidgetModel.build(state(), meta, DataAge(live = false, lastAt = lastAt, why = "x"), Outlook(), conn, now)
        assertFalse(v.live)
        assertEquals("--", v.soc)
        assertEquals(0, v.socPct)
        listOf(v.water, v.gas, v.inside, v.fridge).forEach { assertEquals("--", it.value) }
        assertEquals("Fresh 1", v.water.label)
        assertEquals("No connection · last data " + WidgetModel.clock(lastAt, now), v.status)
        assertEquals("No connection", v.statusShort)
    }

    @Test fun neverConnected() {
        val v = WidgetModel.build(null, Meta(), DataAge(live = false), Outlook(), ConnStatus(), now)
        assertEquals("Open the app to connect", v.status)
        assertEquals("Water", v.water.label)
    }

    @Test fun savedLiveViewGoesStaleWhenTheAppStops() {
        val v = WidgetModel.build(state(), meta, live, Outlook(), conn, now)
        assertTrue(v.at(now + 60_000, now).live)
        val old = v.at(now + WidgetModel.STALE_MS + 1, now)
        assertFalse(old.live)
        assertEquals("--", old.soc)
        assertTrue(old.status.startsWith("No connection · last data"))
    }

    @Test fun jsonRoundTrip() {
        val v = WidgetModel.build(state(), meta, live, Outlook(battery = "Full in 2 h 0 min"), conn, now)
        assertEquals(v, WidgetView.fromJson(v.toJson()))
        assertEquals(null, WidgetView.fromJson("not json"))
    }

    @Test fun pushThrottle() {
        val a = WidgetModel.build(state(), meta, live, Outlook(), conn, now)
        assertTrue(WidgetModel.shouldPush(a, null, 0, now))
        val b = WidgetModel.build(state(soc = 77.0), meta, live, Outlook(), conn, now + 5_000)
        assertFalse(WidgetModel.shouldPush(b, a, now, now + 5_000))   // too soon
        assertTrue(WidgetModel.shouldPush(b, a, now, now + 31_000))
        val gone = WidgetModel.build(state(), meta, DataAge(live = false, lastAt = now), Outlook(), conn, now + 5_000)
        assertTrue(WidgetModel.shouldPush(gone, a, now, now + 5_000)) // lost: straight away
    }

    @Test fun clockText() {
        assertTrue(WidgetModel.clock(now, now).matches(Regex("\\d{1,2}:\\d{2} (am|pm)")))
        assertTrue(WidgetModel.clock(now - 30 * 86_400_000L, now).matches(Regex("\\d{1,2} [A-Z][a-z]{2}")))
    }
}
