package com.github.jaui.adbwifi

import android.Manifest
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.PowerManager
import android.provider.Settings
import android.util.Log

/**
 * Access to the one system setting this app manages, plus the app's own preferences.
 *
 * Android (AdbDebuggingManager) switches Wireless Debugging off by writing 0 to
 * `adb_wifi_enabled` when Wi-Fi is turned off, disconnects, or the access point (BSSID) changes.
 * Writing 1 back starts it again, provided Wi-Fi is connected and the BSSID is trusted.
 * Writing requires WRITE_SECURE_SETTINGS, granted once via adb.
 */
object AdbWifi {
    const val SETTING = "adb_wifi_enabled"
    const val TAG = "adbwifi"

    private const val PREFS = "adbwifi"
    private const val KEY_AUTO = "auto"
    private const val KEY_PAUSED_UNTIL = "paused_until"
    private const val KEY_ATTEMPTS = "attempts"
    private const val KEY_KICK = "kick_pending"
    const val PAUSE_MS = 60 * 60 * 1000L

    private fun prefs(ctx: Context): SharedPreferences =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun isOn(ctx: Context): Boolean =
        Settings.Global.getInt(ctx.contentResolver, SETTING, 0) == 1

    fun hasPermission(ctx: Context): Boolean =
        ctx.checkSelfPermission(Manifest.permission.WRITE_SECURE_SETTINGS) ==
            PackageManager.PERMISSION_GRANTED

    fun isBatteryExempt(ctx: Context): Boolean =
        ctx.getSystemService(PowerManager::class.java)
            .isIgnoringBatteryOptimizations(ctx.packageName)

    /** The connected Wi-Fi network, if any (not only the default network: Wi-Fi without
     *  internet access may not be the default, but adb over Wi-Fi works on it anyway). */
    @Suppress("DEPRECATION")
    fun wifiNetwork(ctx: Context): Network? {
        val cm = ctx.getSystemService(ConnectivityManager::class.java)
        return cm.allNetworks.firstOrNull { n ->
            cm.getNetworkCapabilities(n)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
        }
    }

    fun isAuto(ctx: Context): Boolean = prefs(ctx).getBoolean(KEY_AUTO, true)

    fun setAuto(ctx: Context, value: Boolean) {
        prefs(ctx).edit().putBoolean(KEY_AUTO, value).apply()
    }

    /** End of the current pause (ms since epoch), 0 = not paused. */
    fun pausedUntil(ctx: Context): Long {
        val until = prefs(ctx).getLong(KEY_PAUSED_UNTIL, 0L)
        return if (until > System.currentTimeMillis()) until else 0L
    }

    fun isPaused(ctx: Context): Boolean = pausedUntil(ctx) != 0L

    /**
     * Pause automatic mode for [PAUSE_MS] (or until reboot). Time-based on purpose: an event such
     * as "next Wi-Fi connection" may never come, and an accidental tap on the tile must not
     * switch the automation off for good. The periodic job picks up after the pause.
     */
    fun pause(ctx: Context) {
        prefs(ctx).edit().putLong(KEY_PAUSED_UNTIL, System.currentTimeMillis() + PAUSE_MS).apply()
    }

    fun resume(ctx: Context) {
        prefs(ctx).edit().remove(KEY_PAUSED_UNTIL).apply()
    }

    private fun attempts(ctx: Context): List<Long> =
        prefs(ctx).getString(KEY_ATTEMPTS, "")!!
            .split(',').mapNotNull { it.toLongOrNull() }

    private fun storeAttempts(ctx: Context, list: List<Long>) {
        prefs(ctx).edit().putString(KEY_ATTEMPTS, list.joinToString(",")).apply()
    }

    /** Called at boot: check once that Wireless Debugging really runs (see [kickIfPending]). */
    fun requestKick(ctx: Context) {
        prefs(ctx).edit().putBoolean(KEY_KICK, true).apply()
    }

    /**
     * Android 17 keeps `adb_wifi_enabled=1` across a reboot. If Wi-Fi is not connected yet when
     * the system tries to start Wireless Debugging at boot, it gives up and does not retry, while
     * the setting still says 1 (observed on test device B). Nothing would trigger then.
     *
     * Once Wi-Fi is connected, write 0 once. The content-trigger job sees the change and writes 1
     * again, which starts the service for real. Returns true if 0 was written.
     */
    fun kickIfPending(ctx: Context, reason: String): Boolean {
        if (!prefs(ctx).getBoolean(KEY_KICK, false)) return false
        if (!isAuto(ctx) || !hasPermission(ctx)) {
            prefs(ctx).edit().remove(KEY_KICK).apply()
            return false
        }
        if (wifiNetwork(ctx) == null) {
            Triggers.scheduleWifiJob(ctx) // keep the kick pending until Wi-Fi is there
            return false
        }
        prefs(ctx).edit().remove(KEY_KICK).apply()
        if (!isOn(ctx)) return false // normal path switches on
        Log.i(TAG, "$reason: KICK")
        return write(ctx, 0)
    }

    /** Switches Wireless Debugging off (tile). Returns false without permission. */
    fun disable(ctx: Context): Boolean = write(ctx, 0)

    /**
     * Switches Wireless Debugging on if [Policy] allows it.
     * [manual] = started by the user; this also ends a pause.
     */
    fun enableIfAllowed(ctx: Context, manual: Boolean, reason: String): Policy.Decision {
        if (manual) resume(ctx)
        val now = System.currentTimeMillis()
        val stored = attempts(ctx)
        val decision = Policy.decide(
            Policy.Input(
                hasPermission = hasPermission(ctx),
                adbWifiOn = isOn(ctx),
                wifiConnected = wifiNetwork(ctx) != null,
                autoEnabled = isAuto(ctx),
                paused = isPaused(ctx),
                attempts = stored,
                now = now,
                manual = manual,
            )
        )
        if (decision == Policy.Decision.ENABLE) {
            if (!write(ctx, 1)) return Policy.Decision.NO_PERMISSION
            if (!manual) storeAttempts(ctx, Policy.recentAttempts(stored, now) + now)
        }
        // No Wi-Fi: a one-shot job runs as soon as Wi-Fi is connected again.
        if (decision == Policy.Decision.NO_WIFI && isAuto(ctx)) Triggers.scheduleWifiJob(ctx)
        Log.i(TAG, "$reason: $decision")
        return decision
    }

    private fun write(ctx: Context, value: Int): Boolean = try {
        Settings.Global.putInt(ctx.contentResolver, SETTING, value)
    } catch (e: SecurityException) {
        Log.w(TAG, "WRITE_SECURE_SETTINGS missing")
        false
    }
}
