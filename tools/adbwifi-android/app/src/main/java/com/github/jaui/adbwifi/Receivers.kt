package com.github.jaui.adbwifi

import android.app.job.JobParameters
import android.app.job.JobService
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Boot (after the first unlock) and app update: set up triggers and switch on. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> {
                AdbWifi.resume(ctx)
                Triggers.apply(ctx)
                // Wi-Fi may not be connected yet; then the Wi-Fi job takes over.
                AdbWifi.enableIfAllowed(ctx, manual = false, reason = intent.action!!)
            }
        }
    }
}

/** Runs for all jobs (see [Triggers]). Short work, no thread needed. */
class EnableJobService : JobService() {
    override fun onStartJob(params: JobParameters): Boolean {
        if (params.jobId == Triggers.JOB_CONTENT && AdbWifi.isAuto(this)) {
            Triggers.scheduleContentJob(this)
        }
        AdbWifi.enableIfAllowed(this, manual = false, reason = "job${params.jobId}")
        return false
    }

    override fun onStopJob(params: JobParameters): Boolean = false
}
