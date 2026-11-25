package com.caravan.monitor

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okio.ByteString
import okio.ByteString.Companion.toByteString
import java.io.ByteArrayOutputStream

/**
 * Just enough MQTT 3.1.1 over a secure WebSocket to talk to the hub through the
 * cloud broker (EMQX Cloud Serverless, wss port 8084): connect with a username/password,
 * subscribe, receive, publish at QoS 0, keep-alive pings. No library needed,
 * and it rides on the OkHttp the app already uses.
 */
class MqttWs(
    private val http: OkHttpClient,
    private val listener: Listener,
) {
    interface Listener {
        /** rc 0 = accepted; 4 / 5 = wrong username or password. */
        fun onConnAck(rc: Int)
        /** [retained]: the broker's stored copy, sent on subscribe; it can be hours old. */
        fun onMessage(topic: String, payload: String, retained: Boolean)
        fun onClosed(why: String)
    }

    @Volatile private var ws: WebSocket? = null
    private val buf = ByteArrayOutputStream()
    private var nextId = 1

    fun connect(url: String, clientId: String, user: String, pass: String, keepAliveSec: Int = 60) {
        val req = Request.Builder().url(url).header("Sec-WebSocket-Protocol", "mqtt").build()
        ws = http.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                val vh = ByteArrayOutputStream().apply {
                    str("MQTT"); write(4)                      // protocol name, level 4 = 3.1.1
                    write(0x80 or 0x40 or 0x02)                // username, password, clean session
                    write(keepAliveSec shr 8); write(keepAliveSec and 0xFF)
                    str(clientId); str(user); str(pass)
                }.toByteArray()
                webSocket.send(packet(0x10, vh))
            }
            override fun onMessage(webSocket: WebSocket, bytes: ByteString) = feed(bytes.toByteArray())
            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                webSocket.close(1000, null)
                listener.onClosed("closed by the broker ($code)")
            }
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                listener.onClosed(t.message ?: t.toString())
            }
        })
    }

    fun subscribe(filter: String) {
        val id = nextId++ and 0xFFFF
        val body = ByteArrayOutputStream().apply { write(id shr 8); write(id and 0xFF); str(filter); write(0) }.toByteArray()
        ws?.send(packet(0x82, body))
    }

    fun publish(topic: String, payload: String): Boolean {
        val body = ByteArrayOutputStream().apply { str(topic); write(payload.toByteArray()) }.toByteArray()
        return ws?.send(packet(0x30, body)) ?: false
    }

    fun ping() { ws?.send(byteArrayOf(0xC0.toByte(), 0).toByteString()) }

    fun close() {
        ws?.send(byteArrayOf(0xE0.toByte(), 0).toByteString())  // DISCONNECT
        ws?.close(1000, null)
        ws?.cancel()
        ws = null
    }

    // A WebSocket frame can hold part of a packet, or several.
    @Synchronized private fun feed(data: ByteArray) {
        buf.write(data)
        var b = buf.toByteArray()
        var off = 0
        while (true) {
            if (b.size - off < 2) break
            var len = 0
            var mul = 1
            var i = off + 1
            var done = false
            while (i < b.size && i < off + 5) {
                val d = b[i].toInt() and 0xFF
                len += (d and 0x7F) * mul
                mul *= 128
                i++
                if (d and 0x80 == 0) { done = true; break }
            }
            if (!done || b.size - i < len) break
            handle(b[off].toInt() and 0xFF, b.copyOfRange(i, i + len))
            off = i + len
        }
        buf.reset()
        if (off < b.size) buf.write(b, off, b.size - off)
    }

    private fun handle(h: Int, body: ByteArray) {
        when (h shr 4) {
            2 -> listener.onConnAck(if (body.size >= 2) body[1].toInt() and 0xFF else 255)
            3 -> {
                val qos = (h shr 1) and 3
                val tl = ((body[0].toInt() and 0xFF) shl 8) or (body[1].toInt() and 0xFF)
                val topic = String(body, 2, tl)
                var p = 2 + tl
                if (qos > 0) {
                    val id0 = body[p].toInt() and 0xFF; val id1 = body[p + 1].toInt() and 0xFF
                    p += 2
                    ws?.send(byteArrayOf(0x40, 2, id0.toByte(), id1.toByte()).toByteString())  // PUBACK
                }
                listener.onMessage(topic, String(body, p, body.size - p), (h and 1) == 1)
            }
            // 9 SUBACK, 13 PINGRESP: nothing to do
        }
    }

    private fun packet(header: Int, body: ByteArray): ByteString {
        val out = ByteArrayOutputStream()
        out.write(header)
        var n = body.size
        do {
            var d = n % 128
            n /= 128
            if (n > 0) d = d or 0x80
            out.write(d)
        } while (n > 0)
        out.write(body)
        return out.toByteArray().toByteString()
    }

    private fun ByteArrayOutputStream.str(s: String) {
        val b = s.toByteArray()
        write(b.size shr 8); write(b.size and 0xFF); write(b)
    }
}
