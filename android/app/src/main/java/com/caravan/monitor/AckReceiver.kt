package com.caravan.monitor

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** "Acknowledge" on a notification: silences the hub's buzzer and marks alerts seen. */
class AckReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        CaravanApp.instance.hub.send("ack")
    }
}
