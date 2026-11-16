package com.caravan.monitor

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Restart background alerts after the phone reboots or the app updates. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        MonitorService.sync(context.applicationContext)
    }
}
