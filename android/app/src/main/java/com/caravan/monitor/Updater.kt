package com.caravan.monitor

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings as AndroidSettings
import androidx.core.content.FileProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import java.io.File
import java.util.concurrent.TimeUnit

/**
 * Checks GitHub for a newer app build and installs it.
 *
 * Every CI build is published as a GitHub release "app-build-<n>" with the APK
 * attached, and all builds are signed with the same key (android/app/debug.keystore),
 * so Android installs each one over the last. The repository is private, so
 * the GitHub API needs a token: a fine-grained, read-only token for this one
 * repository that the user pastes into Setup. It stays on the phone.
 */
class Updater(private val context: Context, private val prefs: Prefs) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    private val current = BuildConfig.BUILD_NUMBER
    private val _info = MutableStateFlow(UpdateInfo(initialState(), current))
    val info: StateFlow<UpdateInfo> = _info

    private var assetId = 0L
    private val apk get() = File(File(context.cacheDir, "updates").apply { mkdirs() }, "caravan-update.apk")

    private fun initialState() = if (prefs.updateToken.isBlank()) UpdState.NO_TOKEN else UpdState.IDLE

    fun setToken(token: String) {
        prefs.updateToken = token
        _info.value = UpdateInfo(initialState(), current)
        if (token.isNotBlank()) check(force = true)
    }

    /** Looks for a newer build; at most every 15 minutes unless [force]. */
    fun check(force: Boolean = false) {
        val token = prefs.updateToken
        if (token.isBlank()) { _info.value = UpdateInfo(UpdState.NO_TOKEN, current); return }
        val st = _info.value.state
        if (st == UpdState.CHECKING || st == UpdState.DOWNLOADING || st == UpdState.READY) return
        if (!force && System.currentTimeMillis() - prefs.lastUpdateCheck < 15 * 60_000L && st != UpdState.IDLE) return
        _info.value = _info.value.copy(state = UpdState.CHECKING, message = "")
        scope.launch {
            try {
                val req = Request.Builder()
                    .url("https://api.github.com/repos/${BuildConfig.UPDATE_REPO}/releases?per_page=20")
                    .header("Authorization", "Bearer $token")
                    .header("Accept", "application/vnd.github+json")
                    .build()
                http.newCall(req).execute().use { r ->
                    if (r.code == 401 || r.code == 403 || r.code == 404) {
                        fail("GitHub said no (${r.code}). Check the token can read ${BuildConfig.UPDATE_REPO}.")
                        return@launch
                    }
                    if (!r.isSuccessful) { fail("GitHub error ${r.code}"); return@launch }
                    val latest = newestBuild(JSONArray(r.body!!.string()))
                    prefs.lastUpdateCheck = System.currentTimeMillis()
                    if (latest == null || latest.build <= current) {
                        _info.value = UpdateInfo(UpdState.UP_TO_DATE, current, latest?.build ?: current)
                    } else {
                        assetId = latest.assetId
                        _info.value = UpdateInfo(UpdState.AVAILABLE, current, latest.build, latest.notes)
                    }
                }
            } catch (e: Exception) {
                fail("Couldn't reach GitHub: ${e.message ?: e.javaClass.simpleName}")
            }
        }
    }

    /** Downloads the newer build, then opens Android's install screen. */
    fun download() {
        val token = prefs.updateToken
        if (_info.value.state != UpdState.AVAILABLE || assetId == 0L) return
        _info.value = _info.value.copy(state = UpdState.DOWNLOADING, progress = 0f)
        scope.launch {
            try {
                // The asset endpoint redirects to a short-lived storage URL; OkHttp
                // drops the Authorization header when it follows that redirect.
                val req = Request.Builder()
                    .url("https://api.github.com/repos/${BuildConfig.UPDATE_REPO}/releases/assets/$assetId")
                    .header("Authorization", "Bearer $token")
                    .header("Accept", "application/octet-stream")
                    .build()
                http.newCall(req).execute().use { r ->
                    if (!r.isSuccessful) { fail("Download failed (${r.code})"); return@launch }
                    val body = r.body!!
                    val total = body.contentLength().takeIf { it > 0 } ?: 1L
                    val tmp = File(apk.path + ".part")
                    tmp.outputStream().use { out ->
                        body.byteStream().use { inp ->
                            val buf = ByteArray(64 * 1024)
                            var done = 0L
                            while (true) {
                                val n = inp.read(buf)
                                if (n < 0) break
                                out.write(buf, 0, n)
                                done += n
                                _info.value = _info.value.copy(progress = (done.toFloat() / total).coerceAtMost(1f))
                            }
                        }
                    }
                    tmp.renameTo(apk)
                }
                _info.value = _info.value.copy(state = UpdState.READY, progress = 1f)
                install()
            } catch (e: Exception) {
                fail("Download failed: ${e.message ?: e.javaClass.simpleName}")
            }
        }
    }

    /** The Update / Install button. */
    fun startOrInstall() = if (_info.value.state == UpdState.READY) install() else download()

    /** Opens the install screen (or, the first time, the "allow from this app" setting). */
    fun install() {
        if (!apk.exists()) { _info.value = _info.value.copy(state = UpdState.AVAILABLE); return }
        if (Build.VERSION.SDK_INT >= 26 && !context.packageManager.canRequestPackageInstalls()) {
            context.startActivity(
                Intent(AndroidSettings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${context.packageName}"))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
            _info.value = _info.value.copy(message = "Allow installs from Caravan, then come back and tap Install.")
            return
        }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.updates", apk)
        context.startActivity(
            Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        )
    }

    private fun fail(msg: String) {
        _info.value = _info.value.copy(state = UpdState.ERROR, message = msg)
    }

    data class Release(val build: Int, val assetId: Long, val notes: String)

    companion object {
        private val TAG = Regex("""^app-build-(\d+)$""")

        /** The highest-numbered "app-build-<n>" release that has an APK attached. Pure, for tests. */
        fun newestBuild(releases: JSONArray): Release? =
            (0 until releases.length()).mapNotNull { i ->
                val r = releases.optJSONObject(i) ?: return@mapNotNull null
                if (r.optBoolean("draft")) return@mapNotNull null
                val n = TAG.find(r.optString("tag_name"))?.groupValues?.get(1)?.toIntOrNull() ?: return@mapNotNull null
                val assets = r.optJSONArray("assets") ?: return@mapNotNull null
                val apk = (0 until assets.length()).mapNotNull { assets.optJSONObject(it) }
                    .firstOrNull { it.optString("name").endsWith(".apk") } ?: return@mapNotNull null
                Release(n, apk.optLong("id"), r.optString("body").lineSequence().firstOrNull().orEmpty())
            }.maxByOrNull { it.build }
    }
}
