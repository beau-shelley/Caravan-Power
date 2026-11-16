package com.caravan.monitor.ui

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.caravan.monitor.CaravanApp
import com.caravan.monitor.MonitorService

class MainActivity : ComponentActivity() {
    private val askNotify = registerForActivityResult(ActivityResultContracts.RequestPermission()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (Build.VERSION.SDK_INT >= 33) askNotify.launch(Manifest.permission.POST_NOTIFICATIONS)
        setContent { CaravanTheme { App() } }
    }

    override fun onStart() {
        super.onStart()
        CaravanApp.instance.hub.acquire()
        CaravanApp.instance.updater.check()  // throttled; also resumes a pending install
        MonitorService.sync(this)
    }

    override fun onStop() {
        CaravanApp.instance.hub.release()
        super.onStop()
    }
}

@Composable
private fun App() {
    val hub = CaravanApp.instance.hub
    val state by hub.state.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    val snack = remember { SnackbarHostState() }

    // Show hub errors (e.g. "invalid rule") as a snackbar.
    LaunchedEffect(Unit) {
        hub.replies.collect { r ->
            if (r.optString("t") == "err") snack.showSnackbar("Hub: " + r.optString("msg", "error"))
        }
    }

    AppScaffold(tab, { tab = it }, state?.alerts?.count { !it.ack } ?: 0, snackbarHost = { SnackbarHost(snack) }) {
        when (tab) {
            0 -> HomeScreen(onOpenAlerts = { tab = 1 }, onOpenSetup = { tab = 2 })
            1 -> AlertsScreen()
            3 -> ClimateScreen(onOpenSetup = { tab = 2 })
            else -> SetupScreen()
        }
    }
}
