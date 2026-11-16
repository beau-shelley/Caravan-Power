package com.caravan.monitor

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ModelTest {
    // Shape taken from the demo state in web/index.html.
    private val stateJson = """
        {"t":"state","ts":1731650000,"bat":{"on":true,"src":1,"v":13.31,"a":18.8,"soc":78,"ttg":52,"sa":31.4,"ca":0,"chg":31.4,"load":12.6},
         "sol":[{"on":true,"pv":-1,"w":420,"yt":1.2,"mx":510},{"on":false}],
         "tmp":[{"on":true,"c":24.5,"lo":18,"hi":29},{"on":true,"c":31},{"on":true,"c":3.5}],
         "tank":[{"on":true,"p":80},{"on":true,"p":40},{"on":false,"p":0}],
         "pump":[{"on":true,"sn":true,"lk":false,"rt":12},{"on":false,"lk":true}],
         "alerts":[{"id":3,"sev":2,"ack":false,"since":1731649000,"val":11.6,"msg":"Battery voltage 11.6 V (below 11.8)"}],
         "wx":{"ok":true,"pl":"Bright","t":21.4,"w":12,"g":30,"c":3},
         "net":{"wifi":true,"cloud":false,"ip":"192.168.1.50"}}
    """.trimIndent()

    @Test fun parsesState() {
        val s = HubState.parse(JSONObject(stateJson))
        assertEquals(78.0, s.bat.soc, 0.0)
        assertEquals(52, s.bat.ttgMin)
        assertEquals(2, s.sol.size)
        assertFalse(s.sol[1].on)
        assertEquals(420.0, s.sol[0].w, 0.0)
        assertEquals(3.5, s.tmp[2].c, 0.0)
        assertTrue(s.pump[1].lockout)
        assertEquals(1, s.alerts.size)
        assertEquals(2, s.alerts[0].sev)
        assertEquals("Bright", s.wx.place)
    }

    @Test fun parsesMainsCharging() {
        val s = HubState.parse(JSONObject("""{"t":"state","bat":{"on":true,"a":105,"sa":20,"ca":0,"aca":97.5,"acon":true}}"""))
        assertTrue(s.bat.acOn)
        assertEquals(97.5, s.bat.acA, 0.0)
        assertFalse(HubState.parse(JSONObject(stateJson)).bat.acOn)
    }

    @Test fun emptyStateDoesNotCrash() {
        val s = HubState.parse(JSONObject("{\"t\":\"state\"}"))
        assertFalse(s.bat.on)
        assertTrue(s.alerts.isEmpty())
    }

    @Test fun ruleRoundTripsInHubFormat() {
        val r = Rule(id = 4, name = "Fridge warm", metric = "temp_fridge", above = true, threshold = 8.0,
            hysteresis = 1.5, delaySec = 600, sev = Severity.ALARM, notify = true, action = "none")
        val j = r.toJson()
        assertEquals(">", j.getString("cmp"))
        assertEquals("temp_fridge", j.getString("m"))
        assertEquals(600, j.getInt("dly"))
        assertEquals(r, Rule.parse(j))
    }

    @Test fun describesRuleWithHubNames() {
        val meta = Meta(tankNames = listOf("Fresh 1", "Fresh 2", "Grey"))
        val r = Rule(metric = "tank1", above = false, threshold = 20.0, delaySec = 120)
        assertEquals("Fresh 1 below 20 % for 2 min", r.describe(meta))
    }

    @Test fun parsesMetaWithScreenSleep() {
        val m = Meta.parse(JSONObject("""{"t":"meta","tank":["A","B","C"],"rc":false,"ss":30}"""))
        assertEquals(listOf("A", "B", "C"), m.tankNames)
        assertFalse(m.remoteControl)
        assertEquals(30, m.screenSleepSec)
        assertEquals(-1, Meta.parse(JSONObject("{}")).screenSleepSec)
    }

    @Test fun formatsNumbers() {
        assertEquals("12", fmtNum(12.0))
        assertEquals("12.5", fmtNum(12.5))
        assertEquals("0.1", fmtNum(0.1))
        assertEquals("1 h", formatDelay(3600))
        assertEquals("45 s", formatDelay(45))
    }

    @Test fun ruleLevelsRoundTripAndDropShallowSteps() {
        val r = Rule(4, true, "Battery charge low", "bat_soc", false, 30.0, 5.0, 60, Severity.WARN,
            levels = listOf(Level(15.0, Severity.ALARM), Level(5.0, Severity.CRIT)))
        val back = Rule.parse(org.json.JSONObject(r.toJson().toString()))
        assertEquals(r, back)
        assertEquals(Severity.CRIT, back.maxSev)
        val bad = org.json.JSONObject(r.toJson().toString())
        bad.getJSONArray("lv").getJSONObject(1).put("thr", 20)  // shallower than 15
        assertEquals(1, Rule.parse(bad).levels.size)
        assertFalse(Rule(1, metric = "bat_v").toJson().has("lv"))
    }
}
