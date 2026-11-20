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
        /** Timestamps (ms) of earlier automatic activation attempts. */
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

    // JobScheduler may deliver a settings change late, so elapsed time cannot
    // tell us whether Android accepted an activation. Count each automatic write.
    fun recentAttempts(attempts: List<Long>, now: Long): List<Long> =
        attempts.filter { now - it in 0 until WINDOW_MS }
}
