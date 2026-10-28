package com.retro.n64;

import android.os.Bundle;
import com.arcade.engine.RetroActivity;

/**
 * Nintendo 64 specific NativeActivity host extending core RetroActivity.
 * Hosts Libretro Mupen64Plus-Next OpenGL ES 3.0 hardware-rendered core.
 */
public class N64Activity extends RetroActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }
}
