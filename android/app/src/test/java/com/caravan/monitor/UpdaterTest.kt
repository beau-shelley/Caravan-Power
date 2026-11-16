package com.caravan.monitor

import org.json.JSONArray
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class UpdaterTest {
    private fun rel(tag: String, asset: String?, id: Long = 1, draft: Boolean = false) =
        """{"tag_name":"$tag","draft":$draft,"body":"Notes for $tag\nmore","assets":[${
            if (asset == null) "" else """{"id":$id,"name":"$asset"}"""
        }]}"""

    @Test fun picksHighestBuildWithAnApk() {
        val r = Updater.newestBuild(JSONArray("[" + listOf(
            rel("app-build-20", "caravan-monitor-abc.apk", 20),
            rel("app-build-22", null),                       // no APK yet
            rel("app-build-21", "caravan-monitor-def.apk", 21),
            rel("app-build-30", "caravan-monitor-x.apk", 30, draft = true),
            rel("v1.0", "other.apk", 99),
        ).joinToString(",") + "]"))!!
        assertEquals(21, r.build)
        assertEquals(21L, r.assetId)
        assertEquals("Notes for app-build-21", r.notes)
    }

    @Test fun noneWhenNoBuilds() {
        assertNull(Updater.newestBuild(JSONArray("[]")))
    }
}
