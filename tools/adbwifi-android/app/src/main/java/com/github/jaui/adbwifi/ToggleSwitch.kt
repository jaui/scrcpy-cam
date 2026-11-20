package com.github.jaui.adbwifi

import android.animation.ArgbEvaluator
import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Checkable
import android.widget.Switch

/**
 * Large, plainly drawn on/off switch: a pill-shaped track that turns green with a white knob.
 * Optional "AN"/"AUS" labels inside the track make the state readable without colour.
 * A tap flips the switch and calls [onToggle]; the caller then sets the real state with
 * [setChecked], so a refused change slides back. Reports itself as a Switch to TalkBack.
 */
class ToggleSwitch @JvmOverloads constructor(
    ctx: Context,
    attrs: AttributeSet? = null,
) : View(ctx, attrs), Checkable {

    var onToggle: ((Boolean) -> Unit)? = null
    var colorOn = Color.GREEN
    var colorOff = Color.GRAY
    var labelOn: String? = null
    var labelOff: String? = null
    var labelOffColor = Color.BLACK

    private var checked = false
    /** Knob position, 0 = off (left) … 1 = on (right). */
    private var pos = 0f
    private var anim: ValueAnimator? = null

    private val evaluator = ArgbEvaluator()
    private val track = Paint(Paint.ANTI_ALIAS_FLAG)
    private val knob = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        typeface = Typeface.DEFAULT_BOLD
    }
    private val rect = RectF()

    init {
        isClickable = true
        isFocusable = true
    }

    override fun isChecked() = checked

    override fun setChecked(value: Boolean) {
        if (value == checked) return
        checked = value
        anim?.cancel()
        val target = if (value) 1f else 0f
        if (isLaidOut && isShown) {
            anim = ValueAnimator.ofFloat(pos, target).apply {
                duration = 180
                addUpdateListener {
                    pos = it.animatedValue as Float
                    invalidate()
                }
                start()
            }
        } else {
            pos = target
            invalidate()
        }
    }

    override fun toggle() {
        isChecked = !checked
    }

    override fun performClick(): Boolean {
        toggle()
        onToggle?.invoke(checked)
        return super.performClick()
    }

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat()
        val h = height.toFloat()
        val r = h / 2
        rect.set(0f, 0f, w, h)
        track.color = evaluator.evaluate(pos, colorOff, colorOn) as Int
        canvas.drawRoundRect(rect, r, r, track)

        val inset = h * 0.09f
        val knobR = r - inset
        val cx = r + pos * (w - h)
        knob.setShadowLayer(inset * 0.6f, 0f, inset * 0.25f, 0x40000000)
        canvas.drawCircle(cx, r, knobR, knob)

        // Labels sit in the free part of the track, fading with the knob position.
        text.textSize = h * 0.3f
        val baseline = r - (text.descent() + text.ascent()) / 2
        labelOn?.let {
            text.color = Color.WHITE
            text.alpha = (255 * pos).toInt()
            canvas.drawText(it, (cx - knobR) / 2, baseline, text)
        }
        labelOff?.let {
            text.color = labelOffColor
            text.alpha = (255 * (1 - pos)).toInt()
            canvas.drawText(it, (cx + knobR + w) / 2, baseline, text)
        }
    }

    override fun getAccessibilityClassName(): CharSequence = Switch::class.java.name

    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.isCheckable = true
        info.isChecked = checked
    }
}
