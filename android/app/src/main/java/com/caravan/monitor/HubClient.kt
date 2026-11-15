package com.caravan.monitor

import android.content.Context
import android.os.SystemClock
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import okhttp3.Credentials
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/**
 * Connection to the hub on the caravan's Wi-Fi.
 *
 * 1. GET http://<hub>/api/token (HTTP Basic auth) -> {"tok":..., "wsPort":81}
 * 2. WebSocket ws://<hub>:81/ and send {"c":"auth","tok":...}
 * 3. Send {"c":"hello"} for names + alert rules; the hub then pushes state every second.
 *
 * The connection is held open while anything uses it: the app on screen, or
 * the background alert service.
 */
class HubClient(
    private val context: Context,
    private val prefs: Prefs,
    private val notifier: AlertNotifier,
) {
    enum class Conn { IDLE, SEARCHING, CONNECTING, CONNECTED, AUTH_FAILED, OFFLINE }
    data class Status(val conn: Conn = Conn.IDLE, val host: String = "", val detail: String = "")

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val http = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(8, TimeUnit.SECONDS)
        .pingInterval(15, TimeUnit.SECONDS)
        .build()

    private val _status = MutableStateFlow(Status())
    val status: StateFlow<Status> = _status
    private val _state = MutableStateFlow<HubState?>(null)
    val state: StateFlow<HubState?> = _state
    private val _meta = MutableStateFlow(Meta())
    val meta: StateFlow<Meta> = _meta
    private val _rules = MutableStateFlow<List<Rule>>(emptyList())
    val rules: StateFlow<List<Rule>> = _rules
    /** Replies that aren't state/rules/meta, e.g. {"t":"err","msg":...}. */
    private val _replies = MutableSharedFlow<JSONObject>(extraBufferCapacity = 32)
    val replies: SharedFlow<JSONObject> = _replies

    @Volatile private var ws: WebSocket? = null
    @Volatile private var lastMsgMs = 0L
    private val users = AtomicInteger(0)
    private var loop: Job? = null

    /** True if the hub was heard from in the last [withinMs]. */
    fun recentlyConnected(withinMs: Long = 15_000) =
        _status.value.conn == Conn.CONNECTED && SystemClock.elapsedRealtime() - lastMsgMs < withinMs

    @Synchronized fun acquire() {
        if (users.incrementAndGet() == 1 || loop?.isActive != true) start()
    }

    @Synchronized fun release() {
        if (users.decrementAndGet() <= 0) {
            users.set(0)
            stop()
        }
    }

    /** Drop the connection and start again (after the settings change). */
    @Synchronized fun reconnect() {
        if (users.get() > 0) { stop(); start() }
    }

    fun send(cmd: JSONObject): Boolean {
        val w = ws ?: return false
        return w.send(cmd.toString())
    }

    fun send(c: String, vararg kv: Pair<String, Any>): Boolean {
        val o = JSONObject().put("c", c)
        kv.forEach { (k, v) -> o.put(k, v) }
        return send(o)
    }

    /** Base http URL of the hub the app is talking to, for opening its web page. */
    fun baseUrl(): String? = _status.value.host.takeIf { it.isNotEmpty() }?.let { "http://$it/" }

    private fun start() {
        loop?.cancel()
        loop = scope.launch { runLoop() }
    }

    private fun stop() {
        loop?.cancel()
        loop = null
        ws?.close(1000, null)
        ws = null
        _status.value = Status(Conn.IDLE)
    }

    private suspend fun runLoop() {
        var backoff = 2_000L
        while (scope.isActive) {
            val s = prefs.settings.value
            val candidates = if (s.host.isNotBlank()) listOf(s.host.trim()) else buildList {
                if (prefs.lastHost.isNotBlank()) add(prefs.lastHost)
            }
            var result = tryHosts(candidates, s)
            if (result == null && s.host.isBlank()) {
                _status.value = Status(Conn.SEARCHING, detail = "Looking for the hub on this Wi-Fi")
                val found = Discovery.find(context)
                result = tryHosts(listOfNotNull(found, "caravan.local").distinct(), s)
            }
            when (result) {
                null -> {
                    _status.value = Status(Conn.OFFLINE, detail = "Hub not found. Are you on the caravan's Wi-Fi?")
                }
                is TokenResult.AuthFailed -> {
                    _status.value = Status(Conn.AUTH_FAILED, result.host, "Wrong username or password")
                    delay(20_000)
                    continue
                }
                is TokenResult.Ok -> {
                    if (s.host.isBlank()) prefs.lastHost = result.host
                    backoff = 2_000L
                    runSocket(result)
                }
            }
            delay(backoff)
            backoff = (backoff * 2).coerceAtMost(30_000L)
        }
    }

    private sealed class TokenResult {
        data class Ok(val host: String, val token: String, val wsPort: Int) : TokenResult()
        data class AuthFailed(val host: String) : TokenResult()
    }

    private fun tryHosts(hosts: List<String>, s: Settings): TokenResult? {
        for (h in hosts) {
            _status.value = Status(Conn.CONNECTING, h, "Connecting to $h")
            val r = fetchToken(h, s)
            if (r != null) return r
        }
        return null
    }

    private fun fetchToken(host: String, s: Settings): TokenResult? = try {
        val req = Request.Builder().url("http://$host/api/token").apply {
            if (s.user.isNotEmpty()) header("Authorization", Credentials.basic(s.user, s.pass))
        }.build()
        http.newCall(req).execute().use { resp: Response ->
            when {
                resp.code == 401 -> TokenResult.AuthFailed(host)
                resp.isSuccessful -> {
                    val o = JSONObject(resp.body?.string() ?: "{}")
                    TokenResult.Ok(host, o.optString("tok"), o.optInt("wsPort", 81))
                }
                else -> null
            }
        }
    } catch (e: Exception) {
        Log.d(TAG, "token from $host failed: $e")
        null
    }

    private suspend fun runSocket(t: TokenResult.Ok) {
        val closed = CompletableDeferred<Unit>()
        val hostOnly = t.host.substringBefore(':')
        val req = Request.Builder().url("ws://$hostOnly:${t.wsPort}/").build()
        val socket = http.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                lastMsgMs = SystemClock.elapsedRealtime()
                webSocket.send(JSONObject().put("c", "auth").put("tok", t.token).toString())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                lastMsgMs = SystemClock.elapsedRealtime()
                handle(webSocket, t.host, text)
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                webSocket.close(1000, null)
                closed.complete(Unit)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) { closed.complete(Unit) }

            override fun onFailure(webSocket: WebSocket, tt: Throwable, response: Response?) {
                Log.d(TAG, "socket failed: $tt")
                closed.complete(Unit)
            }
        })
        ws = socket
        try {
            // The hub pushes state every second; if it goes quiet, the link is dead.
            while (!closed.isCompleted) {
                withTimeoutOrNull(5_000) { closed.await() }
                if (SystemClock.elapsedRealtime() - lastMsgMs > 12_000) {
                    socket.cancel()
                    break
                }
            }
        } finally {
            socket.cancel()
            if (ws === socket) ws = null
            if (_status.value.conn == Conn.CONNECTED) _status.value = Status(Conn.OFFLINE, t.host, "Lost connection, retrying")
        }
    }

    private fun handle(socket: WebSocket, host: String, text: String) {
        val o = try { JSONObject(text) } catch (e: Exception) { return }
        when (o.optString("t")) {
            "auth" -> if (o.optBoolean("ok")) {
                _status.value = Status(Conn.CONNECTED, host, "Connected")
                socket.send("{\"c\":\"hello\"}")
            } else {
                _status.value = Status(Conn.AUTH_FAILED, host, "Hub refused the login")
                socket.close(1000, null)
            }
            "state" -> {
                // A hub with no web login accepts us straight away and never sends "auth".
                if (_status.value.conn != Conn.CONNECTED) {
                    _status.value = Status(Conn.CONNECTED, host, "Connected")
                    socket.send("{\"c\":\"hello\"}")
                }
                val st = HubState.parse(o)
                _state.value = st
                notifier.onLocalState(st, _rules.value, _meta.value)
            }
            "hello" -> {
                o.optJSONObject("meta")?.let { _meta.value = Meta.parse(it) }
                _rules.value = Rule.parseList(o.optJSONArray("rules"))
            }
            "rules" -> _rules.value = Rule.parseList(o.optJSONArray("rules"))
            "meta" -> _meta.value = Meta.parse(o)
            else -> _replies.tryEmit(o)
        }
    }

    companion object { private const val TAG = "HubClient" }
}
