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
        val slices = mutableListOf(Slice(start - 6000, 10.0))
        for (k in 0..288) slices += Slice(start + k * 600L, 60.0 - 5.0 * k / 144)
        val now = start + 288 * 600L
        assertEquals(5.0, UsageLog.gasPerDay(slices, now)!!, 0.01)
        assertEquals("10 days left", UsageLog.gasShort(slices, 50.0, now))
        assertEquals("Measuring", UsageLog.gasShort(slices.take(20), 50.0, now))
        // Only the first and last few hours seen (the phone was away in between): same rate.
        val sparse = slices.filterIndexed { i, _ -> i < 20 || i > 270 }
        assertEquals(5.0, UsageLog.gasPerDay(sparse, now)!!, 0.05)
    }

    @Test fun logKeepsGasEveryTenMinutes() {
        val log = UsageLog()
        var t = 1_700_000_400_000L
        var saves = 0
        repeat(3600) { if (log.add(state(gas = 50.0), t)) saves++; t += 1000 }
        assertEquals(5, saves)
        assertEquals(6, log.all().size)
        val back = UsageLog().apply { load(log.lines()) }
        assertEquals(log.slices.toList(), back.slices.toList())
        // A line from the older, wider log still loads its gas reading.
        assertEquals(Slice(600, 63.0), Slice.parse("600,590,3.25,81,63,80|-1,2.5,1.9"))
    }
}
