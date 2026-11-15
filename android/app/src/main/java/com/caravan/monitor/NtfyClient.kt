package com.caravan.monitor

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.util.concurrent.TimeUnit
import kotlin.coroutines.coroutineContext

/**
 * Away-from-the-van alerts. The hub already pushes every alert to an ntfy
 * topic (secrets.h NTFY_TOPIC) when it has internet. This listens to the same
 * topic, so you don't need the separate ntfy app.
 *
 * While the phone is connected to the hub directly, ntfy messages are skipped
 * because the local connection already raised the notification.
 */
class NtfyClient(
    private val prefs: Prefs,
    private val hub: HubClient,
    private val notifier: AlertNotifier,
) {
    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS) // ntfy sends a keepalive every 45 s
        .build()

    suspend fun run() = withContext(Dispatchers.IO) {
        var backoff = 5_000L
        while (coroutineContext.isActive) {
            val s = prefs.settings.value
            if (s.ntfyTopic.isBlank()) { delay(10_000); continue }
            try {
                val since = prefs.ntfyLastId.ifBlank { "10m" }
                val url = "${s.ntfyServer.trimEnd('/')}/${s.ntfyTopic}/json?since=$since"
                val req = Request.Builder().url(url).apply {
                    if (s.ntfyToken.isNotBlank()) header("Authorization", "Bearer ${s.ntfyToken}")
                }.build()
                http.newCall(req).execute().use { resp ->
                    if (!resp.isSuccessful) throw IllegalStateException("HTTP ${resp.code}")
                    backoff = 5_000L
                    val src = resp.body!!.source()
                    while (coroutineContext.isActive) {
                        val line = src.readUtf8Line() ?: break
                        if (line.isBlank()) continue
                        handle(JSONObject(line))
                        // Pick up a changed topic without waiting for the stream to end.
                        if (prefs.settings.value.ntfyTopic != s.ntfyTopic) break
                    }
                }
            } catch (e: Exception) {
                Log.d(TAG, "ntfy: $e")
            }
            delay(backoff)
            backoff = (backoff * 2).coerceAtMost(120_000L)
        }
    }

    private fun handle(o: JSONObject) {
        if (o.optString("event") != "message") return
        prefs.ntfyLastId = o.optString("id")
        if (hub.recentlyConnected()) return
        notifier.onRemote(
            title = o.optString("title").ifBlank { "Caravan" },
            message = o.optString("message"),
            priority = o.optInt("priority", 3),
        )
    }

    companion object { private const val TAG = "Ntfy" }
}
