package com.caravan.monitor

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("caravan", Context.MODE_PRIVATE)
    private val _settings = MutableStateFlow(load())
    val settings: StateFlow<Settings> = _settings

    /** Address the hub was last found at, tried first next time. */
    var lastHost: String
        get() = sp.getString("lastHost", "") ?: ""
        set(v) { sp.edit().putString("lastHost", v).apply() }

    var ntfyLastId: String
        get() = sp.getString("ntfyLastId", "") ?: ""
        set(v) { sp.edit().putString("ntfyLastId", v).apply() }

    private fun load() = Settings(
        host = sp.getString("host", "") ?: "",
        user = sp.getString("user", "admin") ?: "admin",
        pass = sp.getString("pass", "") ?: "",
        backgroundAlerts = sp.getBoolean("bg", true),
        ntfyServer = sp.getString("ntfyServer", "https://ntfy.sh") ?: "https://ntfy.sh",
        ntfyTopic = sp.getString("ntfyTopic", "") ?: "",
        ntfyToken = sp.getString("ntfyToken", "") ?: "",
    )

    fun update(s: Settings) {
        sp.edit()
            .putString("host", s.host.trim())
            .putString("user", s.user.trim())
            .putString("pass", s.pass)
            .putBoolean("bg", s.backgroundAlerts)
            .putString("ntfyServer", s.ntfyServer.trim().trimEnd('/'))
            .putString("ntfyTopic", s.ntfyTopic.trim())
            .putString("ntfyToken", s.ntfyToken.trim())
            .apply()
        _settings.value = load()
    }
}
