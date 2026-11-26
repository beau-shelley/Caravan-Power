package com.caravan.monitor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.material3.NavigationBarDefaults
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Straighten
import androidx.compose.material.icons.filled.Thermostat
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

/**
 * Bottom navigation: Home, Climate, Alerts (with a count of unacknowledged
 * alerts), Level, Setup. Tab numbers stay Home 0, Alerts 1, Setup 2, Climate 3, Level 4.
 */
@OptIn(ExperimentalMaterial3Api::class) // BadgedBox is experimental in older Material 3
@Composable
fun AppScaffold(
    tab: Int,
    onTab: (Int) -> Unit,
    activeAlerts: Int,
    snackbarHost: @Composable () -> Unit = {},
    content: @Composable () -> Unit,
) {
    val itemColors = NavigationBarItemDefaults.colors(
        selectedIconColor = Surfaces.onLime, indicatorColor = Accent.lime,
        selectedTextColor = Accent.lime, unselectedIconColor = Accent.muted, unselectedTextColor = Accent.muted,
    )
    ScaleToScreen { Scaffold(
        modifier = Modifier.caravanBackground(),
        containerColor = Color.Transparent,
        contentColor = Surfaces.text,  // not derived from a transparent container
        snackbarHost = snackbarHost,
        bottomBar = {
            // Dark bar fading down to black, with a thin lime pinstripe on top, like the van.
            Column(Modifier.background(Brush.verticalGradient(listOf(Color(0xFF15171A), Surfaces.plate)))) {
                Box(Modifier.fillMaxWidth().height(1.5.dp).background(Brush.horizontalGradient(listOf(
                    Accent.lime.copy(alpha = 0.15f), Accent.lime.copy(alpha = 0.6f), Accent.lime.copy(alpha = 0.15f)))))
            NavigationBar(containerColor = Color.Transparent, windowInsets = WindowInsets(0, 0, 0, 0)) {
                NavigationBarItem(colors = itemColors, selected = tab == 0, onClick = { onTab(0) },
                    icon = { Icon(Icons.Filled.Home, null) }, label = { Text("Home") })
                NavigationBarItem(colors = itemColors, selected = tab == 3, onClick = { onTab(3) },
                    icon = { Icon(Icons.Filled.Thermostat, null) }, label = { Text("Climate") })
                NavigationBarItem(colors = itemColors, selected = tab == 4, onClick = { onTab(4) },
                    icon = { Icon(Icons.Filled.Straighten, null) }, label = { Text("Level") })
                NavigationBarItem(colors = itemColors, selected = tab == 1, onClick = { onTab(1) },
                    icon = {
                        BadgedBox(badge = { if (activeAlerts > 0) Badge { Text("$activeAlerts") } }) {
                            Icon(Icons.Filled.NotificationsActive, null)
                        }
                    }, label = { Text("Alerts") })
                NavigationBarItem(colors = itemColors, selected = tab == 2, onClick = { onTab(2) },
                    icon = { Icon(Icons.Filled.Settings, null) }, label = { Text("Setup") })
            }
                Spacer(Modifier.fillMaxWidth().windowInsetsBottomHeight(NavigationBarDefaults.windowInsets))
            }
        },
    ) { pad ->
        Box(Modifier.padding(pad)) { content() }
    } }
}
