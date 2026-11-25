package com.caravan.monitor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class UsageTest {
    private fun bat(a: Double, soc: Double = 50.0, ttg: Int = -1, load: Double = 0.0) =
        Battery(on = true, v = 13.1, a = a, soc = soc, ttgMin = ttg, solarA = 0.0, chargerA = 0.0, loadA = load)

    private fun state(a: Double = -5.0, soc: Double = 50.0, gas: Double? = null, tank: Double? = null, fridge: Double? = null, yt: Double = 0.0) = HubState(
        ts = 0, bat = bat(a, soc, load = -a),
        sol = listOf(Solar(true, 0.0, yt, 0.0, 0)),
        tmp = listOf(Temp(false, 0.0, 0.0, 0.0, -1), Temp(false, 0.0, 0.0, 0.0, -1), Temp(fridge != null, fridge ?: 0.0, 0.0, 0.0, -1)),
        tank = listOf(Tank(tank != null, tank ?: 0.0)),
        pump = emptyList(), alerts = emptyList(),
        wx = Weather(false, "", 0.0, 0.0, 0.0, -1), net = Net(true, true, ""),
        gas = Gas(gas != null, gas ?: 0.0),
    )

    @Test fun friendlyDurations() {
        assertEquals("45 min", UsageLog.about(45))
        assertEquals("2 h 10 min", UsageLog.about(130))
        assertEquals("About 9 h", UsageLog.about(9 * 60 + 10))
        assertEquals("About 2 days", UsageLog.about(2 * 1440 + 60))
        assertEquals("About 2½ days", UsageLog.about(2 * 1440 + 720))
        assertEquals("Over 2 weeks", UsageLog.about(20 * 1440))
    }

    @Test fun batteryTimeUsesBankSizeAndAverage() {
        // 405 Ah at 50 % = 202.5 Ah; 4.2 A average -> ~48 h -> about 2 days.
        assertEquals("About 2 days left at this load", UsageLog.batteryText(bat(-10.0), 405, -4.2))
        assertEquals("Full in 1 h 20 min", UsageLog.batteryText(bat(20.0, ttg = 80), 405, 20.0))
        assertEquals("Idle", UsageLog.batteryText(bat(0.1), 405, 0.1))
        assertNull(UsageLog.batteryText(bat(-5.0).copy(on = false), 405, -5.0))
        // Bank size not known yet: falls back to the hub's figure.
        assertEquals("About 3 h left at this load", UsageLog.batteryText(bat(-5.0, ttg = 190), 0, -5.0))
    }

    @Test fun averageSmoothsFridgeCycling() {
        val log = UsageLog()
        var t = 1_700_000_000_000L
        repeat(1800) { i -> log.add(state(a = if ((i / 300) % 2 == 0) -8.0 else -2.0), t); t += 1000 }
        assertTrue(log.avgA in -6.5..-3.5)
    }

    @Test fun gasDaysFromLineFit() {
        val start = 1_700_000_000L
        // 60 % falling 5 % a day over two days, sampled every 10 min, with a refill before it.
        val slices = mutableListOf(Slice(start - 6000, 600, gas = 10.0))
        for (k in 0..288) slices += Slice(start + k * 600L, 600, gas = 60.0 - 5.0 * k / 144)
        val now = start + 288 * 600L
        assertEquals(5.0, UsageLog.gasPerDay(slices, now)!!, 0.01)
        assertEquals("10 days left", UsageLog.gasShort(slices, 50.0, now))
        assertEquals("Measuring", UsageLog.gasShort(slices.take(20), 50.0, now))
    }

    @Test fun slicesRoundTrip() {
        val s = Slice(600, 590, 3.25, 81.0, -1.0, listOf(80.0, -1.0), 2.5, 1.9)
        assertEquals(s, Slice.parse(s.toLine()))
        val n = Slice(1200)
        assertEquals(n.copy(fridgeMin = Double.NaN).toLine(), Slice.parse(n.toLine())!!.toLine())
    }

    @Test fun summaryCoversTheDay() {
        val log = UsageLog()
        val dayStart = { t: Long -> Math.floorDiv(t, 86400L) * 86400L }
        val end = 1_700_006_400L / 86400 * 86400 + 7 * 3600  // 7 am UTC
        var t = end - 26 * 3600L
        var tank = 80.0
        while (t < end) {
            tank -= 10.0 / 1440  // 10 % a day
            log.add(state(a = -4.0, tank = Math.round(tank).toDouble(), fridge = 3.0, yt = 2.5), t * 1000)
            t += 60
        }
        val text = UsageLog.summary(log.all(), end, dayStart, Meta(batteryAh = 405), null, log.avgA)!!
        assertTrue(text, text.contains("Power used: 96 Ah"))
        assertTrue(text, text.contains("Tank 1 down 10%"))
        assertTrue(text, text.contains("Fridge overnight low: 3.0°C"))
        assertTrue(text, text.contains("Solar in yesterday: 2.5 kWh"))
        assertTrue(text, !text.contains("Battery now"))  // nothing "now" without live data
        assertNull(UsageLog.summary(emptyList(), end, dayStart, Meta(), null, Double.NaN))
    }
}
