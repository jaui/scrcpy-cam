package com.github.jaui.adbwifi

/**
 * Decides whether Wireless Debugging should be switched on. No Android dependencies, so it is
 * covered by plain JVM unit tests.
 *
 * Why a rate limit: if the current access point (BSSID) is not trusted, Android switches the
 * setting straight back to 0 and shows the "allow on this network?" dialog. Without a limit the
 * automatic re-enable would loop and pop up that dialog over and over.
 */
object Policy {
    const val MAX_ATTEMPTS = 3
    const val WINDOW_MS = 10 * 60 * 1000L

    enum class Decision {
        ENABLE,
        ALREADY_ON,
        NO_PERMISSION,
        AUTO_OFF,
        PAUSED,
        NO_WIFI,
        RATE_LIMITED,
    }

    data class Input(
        val hasPermission: Boolean,
        val adbWifiOn: Boolean,
        val wifiConnected: Boolean,
        val autoEnabled: Boolean,
        val paused: Boolean,
        /** Timestamps (ms) of earlier failed automatic attempts (see [recentAttempts]). */
        val attempts: List<Long>,
        val now: Long,
        /** Started by the user (tile, launcher icon, button): ignores automatic-mode settings. */
        val manual: Boolean,
    )

    fun decide(input: Input): Decision = when {
        !input.hasPermission -> Decision.NO_PERMISSION
        input.adbWifiOn -> Decision.ALREADY_ON
        !input.manual && !input.autoEnabled -> Decision.AUTO_OFF
        !input.manual && input.paused -> Decision.PAUSED
        !input.wifiConnected -> Decision.NO_WIFI
        !input.manual && recentAttempts(input.attempts, input.now).size >= MAX_ATTEMPTS ->
            Decision.RATE_LIMITED
        else -> Decision.ENABLE
    }

    /**
     * Earlier attempts that count against the rate limit: only failed ones inside the window.
     *
     * An attempt counts as failed if Android switched off again within [STUCK_MS] (the
     * untrusted-network loop: write 1 → system writes 0 → trigger again). If the latest attempt
     * is older than that when the next trigger arrives, it held and is dropped, so ordinary
     * Wi-Fi drops never hit the limit.
     */
    fun recentAttempts(attempts: List<Long>, now: Long): List<Long> {
        val failed = attempts.lastOrNull()
            ?.let { last -> if (now - last >= STUCK_MS) attempts.dropLast(1) else attempts }
            ?: attempts
        return failed.filter { now - it in 0 until WINDOW_MS }
    }

    const val STUCK_MS = 10_000L
}
