package com.caravan.monitor.ui

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
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

    val active = state?.alerts?.count { !it.ack } ?: 0
    Scaffold(
        snackbarHost = { SnackbarHost(snack) },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(selected = tab == 0, onClick = { tab = 0 },
                    icon = { Icon(Icons.Filled.Home, null) }, label = { Text("Home") })
                NavigationBarItem(selected = tab == 1, onClick = { tab = 1 },
                    icon = {
                        BadgedBox(badge = { if (active > 0) Badge { Text("$active") } }) {
                            Icon(Icons.Filled.NotificationsActive, null)
                        }
                    }, label = { Text("Alerts") })
                NavigationBarItem(selected = tab == 2, onClick = { tab = 2 },
                    icon = { Icon(Icons.Filled.Settings, null) }, label = { Text("Setup") })
            }
        },
    ) { pad ->
        Box(Modifier.padding(pad)) {
            when (tab) {
                0 -> HomeScreen(onOpenAlerts = { tab = 1 }, onOpenSetup = { tab = 2 })
                1 -> AlertsScreen()
                else -> SetupScreen()
            }
        }
    }
}
