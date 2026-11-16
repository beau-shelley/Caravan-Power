package com.caravan.monitor

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import android.util.Log
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.coroutines.resume

/**
 * Finds the hub on the local network. The hub advertises "_http._tcp" over
 * mDNS under its hostname ("caravan"). Many Android phones can't open
 * "caravan.local" in a browser, but they can all do DNS-SD through NsdManager.
 */
object Discovery {
    private const val TAG = "Discovery"

    suspend fun find(context: Context, timeoutMs: Long = 8000): String? {
        val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val lock = wifi.createMulticastLock("caravan-discovery").apply { setReferenceCounted(false) }
        lock.acquire()
        try {
            return withTimeoutOrNull(timeoutMs) { discover(context) }
        } finally {
            lock.release()
        }
    }

    @Suppress("DEPRECATION") // resolveService is fine for a one-off resolve
    private suspend fun discover(context: Context): String? = suspendCancellableCoroutine { cont ->
        val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
        var done = false
        lateinit var listener: NsdManager.DiscoveryListener

        fun finish(host: String?) {
            if (done) return
            done = true
            runCatching { nsd.stopServiceDiscovery(listener) }
            if (cont.isActive) cont.resume(host)
        }

        listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {}
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) { done = true; if (cont.isActive) cont.resume(null) }
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
            override fun onServiceLost(service: NsdServiceInfo) {}
            override fun onServiceFound(service: NsdServiceInfo) {
                if (done || !service.serviceName.lowercase().startsWith("caravan")) return
                nsd.resolveService(service, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) {
                        Log.w(TAG, "resolve failed $errorCode")
                    }
                    override fun onServiceResolved(info: NsdServiceInfo) {
                        val addr = info.host?.hostAddress ?: return
                        val port = info.port
                        finish(if (port == 80 || port <= 0) addr else "$addr:$port")
                    }
                })
            }
        }
        nsd.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
        cont.invokeOnCancellation { if (!done) { done = true; runCatching { nsd.stopServiceDiscovery(listener) } } }
    }
}
