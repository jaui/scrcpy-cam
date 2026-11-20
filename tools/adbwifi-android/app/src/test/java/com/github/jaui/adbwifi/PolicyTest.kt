package com.github.jaui.adbwifi

import com.github.jaui.adbwifi.Policy.Decision
import org.junit.Assert.assertEquals
import org.junit.Test

class PolicyTest {

    private val now = 1_000_000_000L

    private fun input(
        hasPermission: Boolean = true,
        adbWifiOn: Boolean = false,
        wifiConnected: Boolean = true,
        autoEnabled: Boolean = true,
        paused: Boolean = false,
        attempts: List<Long> = emptyList(),
        manual: Boolean = false,
    ) = Policy.Input(hasPermission, adbWifiOn, wifiConnected, autoEnabled, paused, attempts, now, manual)

    @Test fun enablesWhenAllowed() = assertEquals(Decision.ENABLE, Policy.decide(input()))

    @Test fun noPermissionWins() =
        assertEquals(Decision.NO_PERMISSION, Policy.decide(input(hasPermission = false, manual = true)))

    @Test fun alreadyOn() = assertEquals(Decision.ALREADY_ON, Policy.decide(input(adbWifiOn = true)))

    @Test fun noWifiNeverWrites() {
        assertEquals(Decision.NO_WIFI, Policy.decide(input(wifiConnected = false)))
        assertEquals(Decision.NO_WIFI, Policy.decide(input(wifiConnected = false, manual = true)))
    }

    @Test fun autoOffBlocksAutomaticOnly() {
        assertEquals(Decision.AUTO_OFF, Policy.decide(input(autoEnabled = false)))
        assertEquals(Decision.ENABLE, Policy.decide(input(autoEnabled = false, manual = true)))
    }

    @Test fun pauseBlocksAutomaticOnly() {
        assertEquals(Decision.PAUSED, Policy.decide(input(paused = true)))
        assertEquals(Decision.ENABLE, Policy.decide(input(paused = true, manual = true)))
    }

    private fun decideWithStored(stored: List<Long>, manual: Boolean = false) =
        Policy.decide(input(attempts = stored, manual = manual))

    @Test fun rateLimitAfterThreeQuickFailures() {
        // untrusted network: Android switched off again right after each attempt
        val loop = listOf(now - 3_000, now - 2_000, now - 1_000)
        assertEquals(Decision.RATE_LIMITED, decideWithStored(loop))
        assertEquals(Decision.ENABLE, decideWithStored(loop, manual = true))
    }

    @Test fun rateLimitAfterThreeAttemptsEvenIfJobsAreDelayed() {
        var stored = emptyList<Long>()
        for (t in listOf(now - 36_000, now - 24_000, now - 12_000)) {
            assertEquals(Decision.ENABLE, Policy.decide(input(attempts = stored).copy(now = t)))
            stored = Policy.recentAttempts(stored, t) + t
        }
        assertEquals(Decision.RATE_LIMITED, decideWithStored(stored))
        assertEquals(Decision.ENABLE, decideWithStored(stored, manual = true))
    }

    @Test fun oldFailuresExpire() {
        val old = listOf(now - Policy.WINDOW_MS - 2_000, now - Policy.WINDOW_MS - 1_000,
            now - Policy.WINDOW_MS)
        assertEquals(emptyList<Long>(), Policy.recentAttempts(old, now))
        assertEquals(Decision.ENABLE, decideWithStored(old))
    }

    @Test fun futureTimestampsIgnored() {
        // clock set back: entries "in the future" must not block forever
        assertEquals(emptyList<Long>(), Policy.recentAttempts(listOf(now + 5_000), now))
    }
}
