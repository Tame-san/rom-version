package com.arcade.engine;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Intent;
import android.os.Bundle;

/**
 * Reusable NativeActivity host for low-latency Libretro emulation.
 * Manages native lifecycle events, intent dispatch, and error dialog presentation.
 */
public class RetroActivity extends NativeActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    /**
     * Called via JNI from native C++ when ROM or core encounters an error.
     */
    public void showFatalError(final String title, final String message) {
        runOnUiThread(() -> {
            new AlertDialog.Builder(this)
                .setTitle(title != null ? title : "Retro Engine Notice")
                .setMessage(message)
                .setPositiveButton("Return to Menu", (dialog, which) -> finish())
                .setCancelable(false)
                .show();
        });
    }
}
