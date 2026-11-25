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
 * Away from the van (hub not found) and with the cloud set up in Setup, it goes
 * through the cloud MQTT broker instead (see docs/CLOUD_SETUP.md): the hub's
 * retained state/meta/rules topics, commands on .../cmd, replies on .../resp.
 * While on the cloud it keeps checking for the hub, and goes direct again as
 * soon as the phone is back on the van's Wi-Fi.
 *
 * The connection is held open while anything uses it: the app on screen, or
 * the background alert service.
 */
class HubClient(
    private val context: Context,
    private val prefs: Prefs,
    private val notifier: AlertNotifier,
) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val http = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(8, TimeUnit.SECONDS)
        .pingInterval(15, TimeUnit.SECONDS)
        .build()

    private val _status = MutableStateFlow(ConnStatus())
    val status: StateFlow<ConnStatus> = _status
    private val _state = MutableStateFlow<HubState?>(null)
    val state: StateFlow<HubState?> = _state
    private val _meta = MutableStateFlow(Meta())
    val meta: StateFlow<Meta> = _meta
    private val _rules = MutableStateFlow<List<Rule>>(emptyList())
    val rules: StateFlow<List<Rule>> = _rules
    private val _age = MutableStateFlow(DataAge(live = false))
    /** Whether [state] is live; the UI greys everything out when it isn't. */
    val age: StateFlow<DataAge> = _age
    /** Replies that aren't state/rules/meta, e.g. {"t":"err","msg":...}. */
    private val _replies = MutableSharedFlow<JSONObject>(extraBufferCapacity = 32)
    val replies: SharedFlow<JSONObject> = _replies

    @Volatile private var ws: WebSocket? = null
    @Volatile private var mqtt: MqttWs? = null
    @Volatile private var cloudBase = ""
    @Volatile private var lastMsgMs = 0L
    /** When the last *fresh* state arrived (elapsedRealtime), and the wall-clock time it's from. */
    @Volatile private var lastFreshMs = 0L
    @Volatile private var dataAtWall = 0L
    /** The cloud's "online" flag: false = the van isn't sending (display or hub down). */
    @Volatile private var cloudHubOnline: Boolean? = null
    private val users = AtomicInteger(0)
    private var loop: Job? = null

    init {
        // Re-check every second, so the screen greys out when the data stops even if the link stays up.
        scope.launch { while (isActive) { updateAge(); delay(1_000) } }
    }

    /**
     * A new state from the hub. [fresh] = it's current (not the cloud's stored copy from
     * some time ago). Only fresh states count as live and feed the phone notifications.
     */
    private fun onState(st: HubState, fresh: Boolean) {
        _state.value = st
        if (fresh) {
            lastFreshMs = SystemClock.elapsedRealtime()
            dataAtWall = System.currentTimeMillis()
            notifier.onLocalState(st, _rules.value, _meta.value)
        } else {
            dataAtWall = if (st.ts > 0) st.ts * 1000 else 0L
        }
        updateAge()
    }

    private fun updateAge() {
        val st = _status.value
        val sinceFresh = SystemClock.elapsedRealtime() - lastFreshMs
        val limit = if (st.cloud) DataAge.CLOUD_STALE_MS else DataAge.LOCAL_STALE_MS
        val why = when {
            st.conn != Conn.CONNECTED -> st.detail.ifBlank { "Not connected to the hub" }
            st.cloud && cloudHubOnline == false -> "The cloud is reachable, but the caravan isn't sending anything"
            lastFreshMs == 0L || sinceFresh > limit ->
                if (st.cloud) "The caravan hasn't sent anything new through the cloud" else "The hub has stopped sending data"
            else -> ""
        }
        val next = DataAge(live = why.isEmpty(), lastAt = dataAtWall, why = why)
        if (next != _age.value) _age.value = next
    }

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
        ws?.let { return it.send(cmd.toString()) }
        val m = mqtt ?: return false
        if (!cmd.has("rid")) cmd.put("rid", java.lang.Long.toHexString(System.nanoTime() and 0xFFFFFF))
        return m.publish(cloudBase + "cmd", cmd.toString())
    }

    fun send(c: String, vararg kv: Pair<String, Any>): Boolean {
        val o = JSONObject().put("c", c)
        kv.forEach { (k, v) -> o.put(k, v) }
        return send(o)
    }

    /** Base http URL of the hub the app is talking to, for opening its web page. */
    fun baseUrl(): String? = _status.value.takeIf { !it.cloud }?.host?.takeIf { it.isNotEmpty() }?.let { "http://$it/" }

    private fun start() {
        loop?.cancel()
        loop = scope.launch { runLoop() }
    }

    private fun stop() {
        loop?.cancel()
        loop = null
        ws?.close(1000, null)
        ws = null
        mqtt?.close()
        mqtt = null
        _status.value = ConnStatus(Conn.IDLE)
        lastFreshMs = 0L
        updateAge()
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
                _status.value = ConnStatus(Conn.SEARCHING, detail = "Looking for the hub on this Wi-Fi")
                val found = Discovery.find(context)
                result = tryHosts(listOfNotNull(found, "caravan.local").distinct(), s)
            }
            when (result) {
                null -> {
                    if (s.cloudOn) {
                        runCloud(s)
                        backoff = 2_000L
                        continue
                    }
                    _status.value = ConnStatus(Conn.OFFLINE, detail = "Hub not found. Are you on the caravan's Wi-Fi?")
                }
                is TokenResult.AuthFailed -> {
                    _status.value = ConnStatus(Conn.AUTH_FAILED, result.host, "Wrong username or password")
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
            _status.value = ConnStatus(Conn.CONNECTING, h, "Connecting to $h")
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
            if (_status.value.conn == Conn.CONNECTED) _status.value = ConnStatus(Conn.OFFLINE, t.host, "Lost connection, retrying")
        }
    }

    /** Through the cloud broker until the link drops or the hub turns up on this Wi-Fi. */
    private suspend fun runCloud(s: Settings) {
        val base = "caravan/${s.cloudDevice.ifBlank { "caravan" }}/"
        val closed = CompletableDeferred<String>()
        val acked = CompletableDeferred<Int>()
        cloudHubOnline = null
        lastFreshMs = 0L  // whatever came direct is no longer current
        fun connected() = ConnStatus(Conn.CONNECTED, "cloud",
            if (cloudHubOnline == false) "Through the cloud, but the caravan is offline" else "Through the cloud", cloud = true)
        _status.value = ConnStatus(Conn.CONNECTING, "cloud", "Away from the van: connecting through the cloud", cloud = true)
        val client = MqttWs(http, object : MqttWs.Listener {
            override fun onConnAck(rc: Int) { acked.complete(rc) }
            override fun onClosed(why: String) { closed.complete(why) }
            override fun onMessage(topic: String, payload: String, retained: Boolean) {
                if (!topic.startsWith(base)) return
                val sub = topic.substring(base.length)
                if (sub == "online") {
                    cloudHubOnline = payload.trim() == "1"
                    if (acked.isCompleted) _status.value = connected()
                    updateAge()
                    return
                }
                if (sub == "cmd" || sub == "event") return
                val o = try { JSONObject(payload) } catch (e: Exception) { return }
                lastMsgMs = SystemClock.elapsedRealtime()
                when (sub) {
                    "state" -> {
                        // The broker's stored copy can be hours old (e.g. the van lost power):
                        // only trust it as live if its own clock says it's recent.
                        val st = HubState.parse(o)
                        val ageS = System.currentTimeMillis() / 1000 - st.ts
                        onState(st, fresh = !retained || (st.ts > 0 && ageS in -120..DataAge.RETAINED_MAX_AGE_S))
                    }
                    "meta" -> _meta.value = Meta.parse(o)
                    "rules" -> _rules.value = Rule.parseList(o.optJSONArray("rules"))
                    "resp" -> when (o.optString("t")) {
                        "hello" -> {
                            o.optJSONObject("meta")?.let { _meta.value = Meta.parse(it) }
                            _rules.value = Rule.parseList(o.optJSONArray("rules"))
                        }
                        "state" -> onState(HubState.parse(o), fresh = true)
                        else -> _replies.tryEmit(o)
                    }
                }
            }
        })
        client.connect(s.cloudUrl, "app-" + java.lang.Long.toHexString(System.nanoTime() and 0xFFFFFFF), s.cloudUser, s.cloudPass)
        try {
            val rc = withTimeoutOrNull(15_000) { kotlinx.coroutines.selects.select<Int> {
                acked.onAwait { it }
                closed.onAwait { -1 }
            } }
            if (rc != 0) {
                _status.value = when (rc) {
                    4, 5 -> ConnStatus(Conn.AUTH_FAILED, "cloud", "The cloud refused the login. Check the cloud username and password.", cloud = true)
                    else -> ConnStatus(Conn.OFFLINE, "cloud", "Hub not found here, and the cloud can't be reached", cloud = true)
                }
                delay(if (rc == 4 || rc == 5) 30_000 else 10_000)
                return
            }
            cloudBase = base
            mqtt = client
            lastMsgMs = SystemClock.elapsedRealtime()
            client.subscribe(base + "#")
            _status.value = connected()
            var lastPing = SystemClock.elapsedRealtime()
            var lastLocalCheck = lastPing
            while (!closed.isCompleted && scope.isActive) {
                withTimeoutOrNull(5_000) { closed.await() }
                val now = SystemClock.elapsedRealtime()
                if (now - lastPing > 25_000) { client.ping(); lastPing = now }
                // Back on the van's Wi-Fi? Go direct (quicker, and works without internet).
                if (now - lastLocalCheck > 30_000) {
                    lastLocalCheck = now
                    val h = s.host.ifBlank { prefs.lastHost }
                    if (h.isNotBlank() && fetchToken(h, s) is TokenResult.Ok) break
                }
            }
        } finally {
            if (mqtt === client) mqtt = null
            client.close()
            if (_status.value.cloud && _status.value.conn == Conn.CONNECTED)
                _status.value = ConnStatus(Conn.OFFLINE, "cloud", "Lost the cloud connection, retrying", cloud = true)
        }
    }

    private fun handle(socket: WebSocket, host: String, text: String) {
        val o = try { JSONObject(text) } catch (e: Exception) { return }
        when (o.optString("t")) {
            "auth" -> if (o.optBoolean("ok")) {
                _status.value = ConnStatus(Conn.CONNECTED, host, "Connected")
                socket.send("{\"c\":\"hello\"}")
            } else {
                _status.value = ConnStatus(Conn.AUTH_FAILED, host, "Hub refused the login")
                socket.close(1000, null)
            }
            "state" -> {
                // A hub with no web login accepts us straight away and never sends "auth".
                if (_status.value.conn != Conn.CONNECTED) {
                    _status.value = ConnStatus(Conn.CONNECTED, host, "Connected")
                    socket.send("{\"c\":\"hello\"}")
                }
                onState(HubState.parse(o), fresh = true)
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
