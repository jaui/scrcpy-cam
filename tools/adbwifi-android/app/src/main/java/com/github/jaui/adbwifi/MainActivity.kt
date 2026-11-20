package com.github.jaui.adbwifi

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.res.ColorStateList
import android.database.ContentObserver
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.text.format.DateFormat
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.Window
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.util.Date

/**
 * Main screen, opened from the launcher icon and by a long press on the quick-settings tile.
 * A large switch turns Wireless Debugging on and off; below it automatic mode and what is still
 * missing for it. Opening the app changes nothing by itself.
 * Plain framework views: no AndroidX, keeps the APK tiny.
 */
class MainActivity : Activity() {

    private lateinit var mainSwitch: ToggleSwitch
    private lateinit var detailText: TextView
    private lateinit var autoSwitch: ToggleSwitch
    private lateinit var permIcon: TextView
    private lateinit var permText: TextView
    private lateinit var batteryIcon: TextView
    private lateinit var batteryText: TextView
    private lateinit var batteryButton: TextView
    private lateinit var setupTitle: TextView

    private val observer = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) = render()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestWindowFeature(Window.FEATURE_NO_TITLE)
        window.setBackgroundDrawable(ColorDrawable(color(R.color.background)))
        setContentView(buildLayout())
        Triggers.apply(this)
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

    private fun color(id: Int) = getColor(id)

    private fun switchMain(on: Boolean) {
        if (!AdbWifi.hasPermission(this)) {
            toast(R.string.toast_no_permission)
        } else if (on) {
            when (AdbWifi.enableIfAllowed(this, manual = true, reason = "app")) {
                Policy.Decision.NO_WIFI -> toast(R.string.toast_no_wifi)
                Policy.Decision.NO_PERMISSION -> toast(R.string.toast_no_permission)
                else -> {}
            }
        } else {
            // Like the tile: pause automatic mode, otherwise it would switch on again at once.
            AdbWifi.pause(this)
            AdbWifi.disable(this)
        }
        render()
    }

    private fun switchAuto(on: Boolean) {
        AdbWifi.setAuto(this, on)
        AdbWifi.resume(this)
        Triggers.apply(this)
        render()
    }

    private fun toast(id: Int) = Toast.makeText(this, id, Toast.LENGTH_LONG).show()

    private fun render() {
        val on = AdbWifi.isOn(this)
        val wifi = AdbWifi.wifiNetwork(this) != null
        val perm = AdbWifi.hasPermission(this)
        val auto = AdbWifi.isAuto(this)
        val pausedUntil = AdbWifi.pausedUntil(this)

        mainSwitch.isChecked = on
        detailText.text = when {
            !perm -> getString(R.string.detail_no_permission)
            on -> getString(R.string.detail_on)
            !wifi -> getString(R.string.detail_no_wifi)
            auto && pausedUntil != 0L -> getString(R.string.detail_paused,
                DateFormat.getTimeFormat(this).format(Date(pausedUntil)))
            else -> getString(R.string.detail_off)
        }
        mainSwitch.contentDescription = getString(R.string.switch_main)

        autoSwitch.isChecked = auto

        // Setup card: a single line when complete, details only when something is missing.
        val exempt = AdbWifi.isBatteryExempt(this)
        val complete = perm && exempt
        setupTitle.visibility = if (complete) View.GONE else View.VISIBLE
        setCheck(permIcon, perm)
        permText.text = when {
            complete -> getString(R.string.setup_complete)
            perm -> getString(R.string.permission_ok)
            else -> getString(R.string.permission_missing, packageName)
        }
        (batteryIcon.parent as View).visibility = if (complete) View.GONE else View.VISIBLE
        setCheck(batteryIcon, exempt)
        batteryText.setText(if (exempt) R.string.battery_ok else R.string.battery_missing)
        batteryButton.visibility = if (exempt) View.GONE else View.VISIBLE
    }

    private fun setCheck(icon: TextView, ok: Boolean) {
        icon.setText(if (ok) R.string.mark_ok else R.string.mark_missing)
        (icon.background as GradientDrawable).setColor(color(if (ok) R.color.on else R.color.warn))
    }

    @SuppressLint("BatteryLife")
    private fun requestBatteryExemption() {
        startActivity(
            Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS)
                .setData(Uri.parse("package:$packageName"))
        )
    }

    // ---- layout ----

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun text(sizeSp: Float, colorId: Int, bold: Boolean = false) = TextView(this).apply {
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        setTextColor(color(colorId))
        if (bold) typeface = Typeface.DEFAULT_BOLD
    }

    private fun rounded(colorId: Int, radiusDp: Int) = GradientDrawable().apply {
        setColor(color(colorId))
        cornerRadius = dp(radiusDp).toFloat()
    }

    private fun card(parent: LinearLayout) = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = rounded(R.color.card, 24)
        setPadding(dp(18), dp(14), dp(18), dp(14))
        parent.addView(this, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply {
            topMargin = dp(12)
        })
    }

    private fun switchColors(s: ToggleSwitch) {
        s.colorOn = color(R.color.on)
        s.colorOff = color(R.color.off)
        s.labelOffColor = color(R.color.text_primary)
    }

    /** A row with a round ✓/! mark in front of a text. */
    private fun checkRow(parent: LinearLayout): Pair<TextView, TextView> {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(4), 0, dp(4))
        }
        val icon = text(15f, R.color.on_text, bold = true).apply {
            gravity = Gravity.CENTER
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL }
        }
        row.addView(icon, LinearLayout.LayoutParams(dp(26), dp(26)).apply { marginEnd = dp(12) })
        val label = text(15f, R.color.text_primary)
        row.addView(label, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        parent.addView(row, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        return icon to label
    }

    private fun buildLayout(): ScrollView {
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(16))
        }

        // Header: symbol + name
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        ImageView(this).apply {
            setImageResource(R.drawable.ic_tile)
            imageTintList = ColorStateList.valueOf(color(R.color.on_text))
            background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(color(R.color.ic_launcher_background))
            }
            setPadding(dp(7), dp(7), dp(7), dp(7))
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            header.addView(this, LinearLayout.LayoutParams(dp(42), dp(42)).apply {
                marginEnd = dp(12)
            })
        }
        header.addView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(text(21f, R.color.text_primary, bold = true).apply {
                setText(R.string.app_name)
            })
            addView(text(12f, R.color.text_secondary).apply {
                text = getString(R.string.version, BuildConfig.VERSION_NAME)
            })
        })
        col.addView(header, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))

        // Main card: the big switch
        card(col).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            addView(text(18f, R.color.text_primary, bold = true).apply {
                setText(R.string.switch_main)
                gravity = Gravity.CENTER
            })
            mainSwitch = ToggleSwitch(this@MainActivity).apply {
                switchColors(this)
                labelOn = getString(R.string.state_on)
                labelOff = getString(R.string.state_off)
                onToggle = { switchMain(it) }
            }
            addView(mainSwitch, LinearLayout.LayoutParams(dp(160), dp(80)).apply {
                topMargin = dp(12)
                bottomMargin = dp(10)
            })
            detailText = text(15f, R.color.text_secondary).apply {
                gravity = Gravity.CENTER
            }
            addView(detailText)
        }

        // Automatic mode: text left, switch right; the whole row is tappable.
        card(col).apply {
            val row = LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val texts = LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                addView(text(16f, R.color.text_primary, bold = true).apply {
                    setText(R.string.auto_switch)
                })
                addView(text(14f, R.color.text_secondary).apply {
                    setText(R.string.auto_hint)
                    setPadding(0, dp(2), 0, 0)
                })
            }
            row.addView(texts, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f).apply {
                marginEnd = dp(16)
            })
            autoSwitch = ToggleSwitch(this@MainActivity).apply {
                switchColors(this)
                contentDescription = getString(R.string.auto_switch)
                onToggle = { switchAuto(it) }
            }
            row.addView(autoSwitch, LinearLayout.LayoutParams(dp(64), dp(36)))
            row.setOnClickListener { autoSwitch.performClick() }
            row.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            addView(row, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        }

        // Setup checks
        card(col).apply {
            setupTitle = text(16f, R.color.text_primary, bold = true).apply {
                setText(R.string.setup_title)
                setPadding(0, 0, 0, dp(4))
            }
            addView(setupTitle)
            checkRow(this).let { (i, t) ->
                permIcon = i
                permText = t.apply { setTextIsSelectable(true) }
            }
            checkRow(this).let { (i, t) -> batteryIcon = i; batteryText = t }
            batteryButton = text(15f, R.color.on_text, bold = true).apply {
                setText(R.string.battery_button)
                gravity = Gravity.CENTER
                setPadding(dp(20), dp(12), dp(20), dp(12))
                background = RippleDrawable(
                    ColorStateList.valueOf(0x40FFFFFF), rounded(R.color.accent, 24), null)
                isClickable = true
                isFocusable = true
                setOnClickListener { requestBatteryExemption() }
            }
            addView(batteryButton, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT).apply {
                topMargin = dp(6)
                marginStart = dp(38)
            })
        }

        return ScrollView(this).apply {
            fitsSystemWindows = true
            setBackgroundColor(color(R.color.background))
            addView(col)
        }
    }
}
