package com.example.jokerprotocol

import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class JokerUiTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun testOnboardingAndDiscoveryFlow() {
        // Disable auto-advance globally so the test NEVER waits for infinite animations
        composeTestRule.mainClock.autoAdvance = false

        // 1. Verify Onboarding is displayed on first launch
        composeTestRule.onNodeWithText("Welcome to JOKER").assertExists()

        // 2. Click "Got it" to dismiss Onboarding
        composeTestRule.onNodeWithText("Got it").performClick()
        composeTestRule.mainClock.advanceTimeBy(500)

        // 3. Verify main UI is present
        composeTestRule.onNodeWithText("Discover Peers").assertExists()
        
        // 4. Click "Discover Peers"
        composeTestRule.onNodeWithText("Discover Peers").performClick()
        composeTestRule.mainClock.advanceTimeBy(500)

        // 5. Verify UI state changes to scanning
        composeTestRule.onNodeWithText("Scanning for Peers...").assertExists()
        
        // 6. Test Help button reopening Onboarding
        composeTestRule.onNodeWithText("Help / Manual").performClick()
        composeTestRule.mainClock.advanceTimeBy(500)
        composeTestRule.onNodeWithText("This app demonstrates the JOKER Protocol").assertExists()
    }
}
