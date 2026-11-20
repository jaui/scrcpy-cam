package com.github.jaui.adbwifi

import android.annotation.SuppressLint
import android.app.PendingIntent
import android.content.Intent
import android.database.ContentObserver
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.text.format.DateFormat
import java.util.Date

/**
 * Quick-settings tile. On → off pauses automatic mode for one hour (or until reboot), so an
 * accidental tap cannot switch the automation off for good (that is only possible in the app). Bound only while the shade is open; the observer lives exactly that long.
 */
class AdbWifiTileService : TileService() {

    private val observer = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) = render()
    }

    override fun onStartListening() {
        super.onStartListening()
        contentResolver.registerContentObserver(
            Settings.Global.getUriFor(AdbWifi.SETTING), false, observer)
        render()
    }

    override fun onStopListening() {
        contentResolver.unregisterContentObserver(observer)
        super.onStopListening()
    }

    override fun onClick() {
        super.onClick()
        if (!AdbWifi.hasPermission(this)) {
            openApp()
            return
        }
        if (AdbWifi.isOn(this)) {
            AdbWifi.pause(this)
            AdbWifi.disable(this)
        } else {
            AdbWifi.enableIfAllowed(this, manual = true, reason = "tile")
        }
        render()
    }

    private fun render() {
        val tile = qsTile ?: return
        val on = AdbWifi.isOn(this)
        tile.state = if (on) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        tile.label = getString(R.string.tile_label)
        val pausedUntil = AdbWifi.pausedUntil(this)
        tile.subtitle = when {
            !AdbWifi.hasPermission(this) -> getString(R.string.status_no_permission)
            !AdbWifi.isAuto(this) -> getString(R.string.tile_auto_off)
            pausedUntil != 0L -> getString(R.string.tile_paused,
                DateFormat.getTimeFormat(this).format(Date(pausedUntil)))
            else -> getString(R.string.tile_auto_on)
        }
        tile.updateTile()
    }

    // The Intent overload is only used below API 34, where it is the only one available.
    @SuppressLint("StartActivityAndCollapseDeprecated")
    private fun openApp() {
        val intent = Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startActivityAndCollapse(
                PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_IMMUTABLE))
        } else {
            @Suppress("DEPRECATION")
            startActivityAndCollapse(intent)
        }
    }
}
