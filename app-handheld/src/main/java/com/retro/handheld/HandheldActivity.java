package com.retro.handheld;

import android.os.Bundle;
import com.arcade.engine.RetroActivity;

/**
 * Handheld-specific NativeActivity host extending core RetroActivity.
 * Hosts Libretro Gambatte (Game Boy/GBC) and mGBA (Game Boy Advance) cores.
 */
public class HandheldActivity extends RetroActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }
}
