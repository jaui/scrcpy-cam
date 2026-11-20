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
        Policy.decide(input(attempts = Policy.recentAttempts(stored, now), manual = manual))

    @Test fun rateLimitAfterThreeQuickFailures() {
        // untrusted network: Android switched off again right after each attempt
        val loop = listOf(now - 3_000, now - 2_000, now - 1_000)
        assertEquals(Decision.RATE_LIMITED, decideWithStored(loop))
        assertEquals(Decision.ENABLE, decideWithStored(loop, manual = true))
    }

    @Test fun attemptThatHeldIsNotCounted() {
        // two earlier failures, then an attempt that held for a while
        val stored = listOf(now - 120_000, now - 119_000, now - 60_000)
        assertEquals(listOf(now - 120_000, now - 119_000), Policy.recentAttempts(stored, now))
        assertEquals(Decision.ENABLE, decideWithStored(stored))
    }

    @Test fun repeatedWifiDropsNeverLimited() {
        // each attempt held >10 s: list never grows beyond the one latest entry
        var stored = emptyList<Long>()
        for (t in listOf(now - 300_000, now - 200_000, now - 100_000)) {
            stored = Policy.recentAttempts(stored, t) + t
        }
        assertEquals(listOf(now - 100_000), stored)
        assertEquals(emptyList<Long>(), Policy.recentAttempts(stored, now))
        assertEquals(Decision.ENABLE, decideWithStored(stored))
    }

    @Test fun oldFailuresExpire() {
        val old = listOf(now - Policy.WINDOW_MS - 2_000, now - Policy.WINDOW_MS - 1_000,
            now - Policy.WINDOW_MS)
        assertEquals(emptyList<Long>(), Policy.recentAttempts(old, now))
    }

    @Test fun futureTimestampsIgnored() {
        // clock set back: entries "in the future" must not block forever
        assertEquals(emptyList<Long>(), Policy.recentAttempts(listOf(now + 5_000), now))
    }
}
