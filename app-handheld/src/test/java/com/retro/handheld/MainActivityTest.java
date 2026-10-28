package com.retro.handheld;

import org.junit.Test;
import static org.junit.Assert.*;

public class MainActivityTest {

    @Test
    public void testIsSupportedHandheldRom() {
        assertTrue(MainActivity.isSupportedHandheldRom("pokemon_red.gb"));
        assertTrue(MainActivity.isSupportedHandheldRom("zelda_dx.gbc"));
        assertTrue(MainActivity.isSupportedHandheldRom("metroid_fusion.gba"));
        assertTrue(MainActivity.isSupportedHandheldRom("mario_land.zip"));
        assertTrue(MainActivity.isSupportedHandheldRom("POKEMON.GB"));
        assertTrue(MainActivity.isSupportedHandheldRom("GAME.GBA"));

        assertFalse(MainActivity.isSupportedHandheldRom("super_mario.nes"));
        assertFalse(MainActivity.isSupportedHandheldRom("sonic.smd"));
        assertFalse(MainActivity.isSupportedHandheldRom(null));
        assertFalse(MainActivity.isSupportedHandheldRom(""));
    }

    @Test
    public void testDetectSystem() {
        assertEquals("GAME BOY", MainActivity.detectSystem("pokemon_red.gb"));
        assertEquals("GBC", MainActivity.detectSystem("zelda_dx.gbc"));
        assertEquals("GBA", MainActivity.detectSystem("metroid_fusion.gba"));

        // Case insensitivity
        assertEquals("GAME BOY", MainActivity.detectSystem("TETRIS.GB"));
        assertEquals("GBC", MainActivity.detectSystem("WARIO.GBC"));
        assertEquals("GBA", MainActivity.detectSystem("POKEMON_EMERALD.GBA"));

        // Zip heuristics
        assertEquals("GBC", MainActivity.detectSystem("pokemon_crystal_gbc.zip"));
        assertEquals("GBA", MainActivity.detectSystem("sonic_advance.zip"));
        assertEquals("GBC", MainActivity.detectSystem("shantae_color.zip"));
        assertEquals("GAME BOY", MainActivity.detectSystem("super_mario_land.zip"));

        // Null / fallback
        assertEquals("GAME BOY", MainActivity.detectSystem(null));
    }

    @Test
    public void testFormatFriendlyName() {
        assertEquals("Pokemon Red", MainActivity.formatFriendlyName("Pokemon_Red.gb"));
        assertEquals("Zelda Link's Awakening", MainActivity.formatFriendlyName("Zelda_Link's_Awakening.gbc"));
        assertEquals("Metroid Fusion", MainActivity.formatFriendlyName("Metroid_Fusion.gba"));
        assertEquals("Golden Sun", MainActivity.formatFriendlyName("Golden_Sun.gba.zip"));
        assertEquals("Handheld Game", MainActivity.formatFriendlyName(null));
        assertEquals("Handheld Game", MainActivity.formatFriendlyName(""));
    }
}
