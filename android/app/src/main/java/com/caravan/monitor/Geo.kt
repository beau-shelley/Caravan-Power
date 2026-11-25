package com.caravan.monitor

import android.annotation.SuppressLint
import android.content.Context
import android.location.Geocoder
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import android.os.SystemClock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlin.coroutines.resume

/**
 * Weather location for the hub, worked out on the phone (it has internet and
 * GPS; the hub doesn't need either for this): the phone's own position, or a
 * town search. The result is sent to the hub with location_set.
 */
object Geo {
    private val http = OkHttpClient.Builder()
        .connectTimeout(8, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()

    class GeoError(msg: String) : Exception(msg)

    /** Australian towns matching [q] (name or postcode), via Open-Meteo's free geocoder. */
    suspend fun search(q: String): List<Place> = withContext(Dispatchers.IO) {
        val url = "https://geocoding-api.open-meteo.com/v1/search".toHttpUrl().newBuilder()
            .addQueryParameter("name", q.trim())
            .addQueryParameter("count", "8")
            .addQueryParameter("language", "en")
            .addQueryParameter("countryCode", "AU")
            .build()
        val body = try {
            http.newCall(Request.Builder().url(url).build()).execute().use { it.body?.string() ?: "{}" }
        } catch (e: Exception) {
            throw GeoError("Town search needs internet on this phone")
        }
        JSONObject(body).optJSONArray("results").objects().map {
            val state = stateCode(it.optString("admin1"))
            val pc = it.optJSONArray("postcodes")?.optString(0).orEmpty()
            Place(
                name = listOf(it.optString("name"), state).filter { s -> s.isNotBlank() }.joinToString(" "),
                detail = listOf(pc, it.optString("admin2")).filter { s -> s.isNotBlank() }.joinToString(", "),
                lat = it.optDouble("latitude"), lon = it.optDouble("longitude"), src = Place.SRC_SEARCH,
            )
        }
    }

    /** Where the phone is now, with a town name when one can be found. Needs the location permission. */
    suspend fun here(ctx: Context): Place {
        val loc = fix(ctx) ?: throw GeoError("Couldn't get a location fix. Is Location turned on?")
        val lat = Math.round(loc.latitude * 10000) / 10000.0
        val lon = Math.round(loc.longitude * 10000) / 10000.0
        val name = withContext(Dispatchers.IO) { townAt(ctx, lat, lon) }
        return Place(name.ifBlank { "This phone's spot" }, String.format(Locale.US, "%.4f, %.4f", lat, lon), lat, lon, Place.SRC_PHONE)
    }

    @SuppressLint("MissingPermission")  // callers ask for ACCESS_COARSE_LOCATION first
    private suspend fun fix(ctx: Context): Location? {
        val lm = ctx.getSystemService(LocationManager::class.java) ?: return null
        val providers = listOf(LocationManager.NETWORK_PROVIDER, LocationManager.GPS_PROVIDER)
            .filter { runCatching { lm.isProviderEnabled(it) }.getOrDefault(false) }
        if (providers.isEmpty()) throw GeoError("Turn on Location in the phone's quick settings, then try again")
        // A fix from the last 10 minutes is good enough for the weather.
        val recent = (providers + LocationManager.PASSIVE_PROVIDER)
            .mapNotNull { runCatching { lm.getLastKnownLocation(it) }.getOrNull() }
            .filter { SystemClock.elapsedRealtimeNanos() - it.elapsedRealtimeNanos < 10 * 60 * 1_000_000_000L }
            .minByOrNull { it.accuracy }
        if (recent != null) return recent
        return withTimeoutOrNull(25_000) {
            suspendCancellableCoroutine { cont ->
                val listener = object : LocationListener {
                    override fun onLocationChanged(l: Location) {
                        lm.removeUpdates(this)
                        if (cont.isActive) cont.resume(l)
                    }
                    @Deprecated("Deprecated in Java")
                    override fun onStatusChanged(p: String?, s: Int, b: android.os.Bundle?) {}
                    override fun onProviderEnabled(p: String) {}
                    override fun onProviderDisabled(p: String) {}
                }
                providers.forEach { lm.requestLocationUpdates(it, 0L, 0f, listener, Looper.getMainLooper()) }
                cont.invokeOnCancellation { lm.removeUpdates(listener) }
            }
        }
    }

    /** "Yellow Rock NSW" style name, or "" if nothing answers. */
    private fun townAt(ctx: Context, lat: Double, lon: Double): String {
        try {  // free, no key; same service as the hub's web page
            val url = "https://api.bigdatacloud.net/data/reverse-geocode-client".toHttpUrl().newBuilder()
                .addQueryParameter("latitude", lat.toString())
                .addQueryParameter("longitude", lon.toString())
                .addQueryParameter("localityLanguage", "en")
                .build()
            val o = http.newCall(Request.Builder().url(url).build()).execute().use { JSONObject(it.body?.string() ?: "{}") }
            val town = o.optString("locality").ifBlank { o.optString("city") }
            val st = o.optString("principalSubdivisionCode").substringAfter('-', "")
            if (town.isNotBlank()) return listOf(town, st).filter { it.isNotBlank() }.joinToString(" ")
        } catch (_: Exception) {}
        return try {
            if (!Geocoder.isPresent()) "" else {
                @Suppress("DEPRECATION")
                val a = Geocoder(ctx, Locale("en", "AU")).getFromLocation(lat, lon, 1)?.firstOrNull()
                listOfNotNull(a?.locality ?: a?.subAdminArea, a?.adminArea?.let(::stateCode)).joinToString(" ")
            }
        } catch (_: Exception) { "" }
    }

    private fun stateCode(s: String) = when (s.lowercase(Locale.US)) {
        "new south wales" -> "NSW"
        "victoria" -> "VIC"
        "queensland" -> "QLD"
        "south australia" -> "SA"
        "western australia" -> "WA"
        "tasmania" -> "TAS"
        "northern territory" -> "NT"
        "australian capital territory" -> "ACT"
        else -> s
    }

}
