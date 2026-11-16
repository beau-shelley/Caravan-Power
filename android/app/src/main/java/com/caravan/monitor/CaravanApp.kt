package com.caravan.monitor

import android.app.Application

class CaravanApp : Application() {
    lateinit var prefs: Prefs
        private set
    lateinit var hub: HubClient
        private set
    lateinit var notifier: AlertNotifier
        private set
    lateinit var updater: Updater
        private set

    override fun onCreate() {
        super.onCreate()
        instance = this
        prefs = Prefs(this)
        notifier = AlertNotifier(this)
        hub = HubClient(this, prefs, notifier)
        updater = Updater(this, prefs)
        MonitorService.sync(this)
    }

    companion object {
        lateinit var instance: CaravanApp
            private set
    }
}
