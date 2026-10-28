package com.retro.n64;

import org.junit.Test;
import static org.junit.Assert.*;

public class MainActivityTest {

    @Test
    public void testIsSupportedN64Rom() {
        assertTrue(MainActivity.isSupportedN64Rom("mario64.z64"));
        assertTrue(MainActivity.isSupportedN64Rom("zelda_oot.n64"));
        assertTrue(MainActivity.isSupportedN64Rom("goldeneye.v64"));
        assertTrue(MainActivity.isSupportedN64Rom("smash_bros.zip"));
        assertTrue(MainActivity.isSupportedN64Rom("MARIO64.Z64"));
        assertTrue(MainActivity.isSupportedN64Rom("ZELDA.N64"));
        assertTrue(MainActivity.isSupportedN64Rom("GAME.V64"));
        assertTrue(MainActivity.isSupportedN64Rom("BANJO.ZIP"));

        assertFalse(MainActivity.isSupportedN64Rom("pokemon.gb"));
        assertFalse(MainActivity.isSupportedN64Rom("sonic.smd"));
        assertFalse(MainActivity.isSupportedN64Rom("game.iso"));
        assertFalse(MainActivity.isSupportedN64Rom(null));
        assertFalse(MainActivity.isSupportedN64Rom(""));
    }

    @Test
    public void testDetectSystem() {
        assertEquals("N64", MainActivity.detectSystem("mario64.z64"));
        assertEquals("N64", MainActivity.detectSystem("zelda_oot.n64"));
        assertEquals("N64", MainActivity.detectSystem("goldeneye.v64"));
        assertEquals("N64", MainActivity.detectSystem("smash.zip"));
        assertEquals("N64", MainActivity.detectSystem(null));
    }

    @Test
    public void testFormatFriendlyName() {
        assertEquals("Super Mario 64", MainActivity.formatFriendlyName("Super_Mario_64.z64"));
        assertEquals("The Legend of Zelda Ocarina of Time", MainActivity.formatFriendlyName("The_Legend_of_Zelda_Ocarina_of_Time.n64"));
        assertEquals("GoldenEye 007", MainActivity.formatFriendlyName("GoldenEye_007.v64"));
        assertEquals("Super Smash Bros", MainActivity.formatFriendlyName("Super_Smash_Bros.z64.zip"));
        assertEquals("Mario Kart 64", MainActivity.formatFriendlyName("Mario_Kart_64.zip"));
        assertEquals("N64 Game", MainActivity.formatFriendlyName(null));
        assertEquals("N64 Game", MainActivity.formatFriendlyName(""));
    }
}
