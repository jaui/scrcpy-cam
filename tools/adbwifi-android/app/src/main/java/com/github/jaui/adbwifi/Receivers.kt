package com.github.jaui.adbwifi

import android.app.job.JobParameters
import android.app.job.JobService
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Boot (after the first unlock) and app update: set up triggers and switch on. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val action = intent.action ?: return
        when (action) {
            Intent.ACTION_BOOT_COMPLETED, Intent.ACTION_MY_PACKAGE_REPLACED -> {
                AdbWifi.resume(ctx)
                // Only at boot: an update installed over adb must not drop that connection.
                if (action == Intent.ACTION_BOOT_COMPLETED) AdbWifi.requestKick(ctx)
                Triggers.apply(ctx)
                // A kick writes 0; the content-trigger job then switches on again.
                // Without Wi-Fi yet, the Wi-Fi job takes over.
                if (!AdbWifi.kickIfPending(ctx, action)) {
                    AdbWifi.enableIfAllowed(ctx, manual = false, reason = action)
                }
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
        val reason = "job${params.jobId}"
        if (!AdbWifi.kickIfPending(this, reason)) {
            AdbWifi.enableIfAllowed(this, manual = false, reason = reason)
        }
        return false
    }

    override fun onStopJob(params: JobParameters): Boolean = false
}
