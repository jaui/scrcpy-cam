package com.github.jaui.adbwifi

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.database.ContentObserver
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView

/**
 * Launcher entry. Opening the app switches Wireless Debugging on (that is what the icon is
 * for) and shows the state plus what is still missing for automatic mode.
 * Plain framework views: no AndroidX, keeps the APK tiny.
 */
class MainActivity : Activity() {

    private lateinit var status: TextView
    private lateinit var permission: TextView
    private lateinit var battery: TextView
    private lateinit var batteryButton: Button
    private lateinit var autoSwitch: Switch

    private val observer = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) = render()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildLayout())
        Triggers.apply(this)
        if (savedInstanceState == null) {
            AdbWifi.enableIfAllowed(this, manual = true, reason = "launcher")
        }
    }

    override fun onResume() {
        super.onResume()
        contentResolver.registerContentObserver(
            Settings.Global.getUriFor(AdbWifi.SETTING), false, observer)
        render()
    }

    override fun onPause() {
        contentResolver.unregisterContentObserver(observer)
        super.onPause()
    }

    private fun render() {
        val on = AdbWifi.isOn(this)
        val wifi = AdbWifi.wifiNetwork(this) != null
        status.text = getString(
            when {
                on -> R.string.status_on
                !wifi -> R.string.status_no_wifi
                else -> R.string.status_off
            }
        )
        val perm = AdbWifi.hasPermission(this)
        permission.text = if (perm) getString(R.string.permission_ok)
        else getString(R.string.permission_missing, packageName)
        val exempt = AdbWifi.isBatteryExempt(this)
        battery.text = getString(if (exempt) R.string.battery_ok else R.string.battery_missing)
        batteryButton.visibility = if (exempt) View.GONE else View.VISIBLE
        autoSwitch.isChecked = AdbWifi.isAuto(this)
    }

    @SuppressLint("BatteryLife")
    private fun requestBatteryExemption() {
        startActivity(
            Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS)
                .setData(Uri.parse("package:$packageName"))
        )
    }

    private fun buildLayout(): ScrollView {
        val pad = dp(20)
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        fun text(sizeSp: Float, selectable: Boolean = false) = TextView(this).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
            setTextIsSelectable(selectable)
            setPadding(0, dp(8), 0, dp(8))
            col.addView(this, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        }
        status = text(22f)
        Button(this).apply {
            setText(R.string.button_enable)
            setOnClickListener {
                AdbWifi.enableIfAllowed(this@MainActivity, manual = true, reason = "button")
                render()
            }
            col.addView(this)
        }
        autoSwitch = Switch(this).apply {
            setText(R.string.auto_switch)
            setPadding(0, dp(16), 0, dp(8))
            setOnCheckedChangeListener { _, checked ->
                if (checked != AdbWifi.isAuto(this@MainActivity)) {
                    AdbWifi.setAuto(this@MainActivity, checked)
                    AdbWifi.resume(this@MainActivity)
                    Triggers.apply(this@MainActivity)
                }
            }
            col.addView(this, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        }
        text(14f).setText(R.string.auto_hint)
        permission = text(14f, selectable = true)
        battery = text(14f)
        batteryButton = Button(this).apply {
            setText(R.string.battery_button)
            setOnClickListener { requestBatteryExemption() }
            col.addView(this)
        }
        text(12f).text = getString(R.string.version, BuildConfig.VERSION_NAME)
        return ScrollView(this).apply {
            fitsSystemWindows = true
            addView(col)
        }
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}
