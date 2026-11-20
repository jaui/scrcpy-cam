package com.github.jaui.adbwifi

import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.provider.Settings

/**
 * Event sources for automatic mode. All are JobScheduler jobs; none keeps a process running.
 *
 * - [JOB_CONTENT]: content trigger on `adb_wifi_enabled`, fires when Android writes 0 (Wi-Fi
 *   lost, access point changed). Main trigger, because roaming between access points is not a
 *   new Wi-Fi connection. Content-trigger jobs fire once and cannot be persisted, so they are
 *   re-scheduled after each run and at boot.
 * - [JOB_WIFI]: one-shot job with a Wi-Fi network constraint, scheduled when an attempt finds no
 *   Wi-Fi; runs when Wi-Fi is back. (A PendingIntent network callback is not usable here: the
 *   system sends it once and releases it 5 s later.)
 * - [JOB_PERIODIC]: every 15 min on an unmetered network; safety net, also ends a pause.
 */
object Triggers {
    const val JOB_CONTENT = 1
    const val JOB_PERIODIC = 2
    const val JOB_WIFI = 3
    private const val PERIODIC_MS = 15 * 60 * 1000L

    private fun js(ctx: Context) = ctx.getSystemService(JobScheduler::class.java)

    private fun builder(ctx: Context, id: Int) =
        JobInfo.Builder(id, ComponentName(ctx, EnableJobService::class.java))

    /** Registers or removes all triggers according to the automatic-mode setting. */
    fun apply(ctx: Context) {
        if (AdbWifi.isAuto(ctx)) {
            scheduleContentJob(ctx)
            schedulePeriodicJob(ctx)
            if (AdbWifi.wifiNetwork(ctx) == null) scheduleWifiJob(ctx)
        } else {
            js(ctx).cancel(JOB_CONTENT)
            js(ctx).cancel(JOB_PERIODIC)
            js(ctx).cancel(JOB_WIFI)
        }
    }

    fun scheduleContentJob(ctx: Context) {
        val uri = Settings.Global.getUriFor(AdbWifi.SETTING)
        js(ctx).schedule(
            builder(ctx, JOB_CONTENT)
                .addTriggerContentUri(JobInfo.TriggerContentUri(uri, 0))
                .setTriggerContentUpdateDelay(500)
                .setTriggerContentMaxDelay(3000)
                .build()
        )
    }

    fun scheduleWifiJob(ctx: Context) {
        if (js(ctx).getPendingJob(JOB_WIFI) != null) return
        val wifi = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()
        js(ctx).schedule(builder(ctx, JOB_WIFI).setRequiredNetwork(wifi).build())
    }

    private fun schedulePeriodicJob(ctx: Context) {
        if (js(ctx).getPendingJob(JOB_PERIODIC) != null) return
        js(ctx).schedule(
            builder(ctx, JOB_PERIODIC)
                .setPeriodic(PERIODIC_MS)
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_UNMETERED)
                .setPersisted(true)
                .build()
        )
    }
}
