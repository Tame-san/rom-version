package com.arcade.engine;

import android.app.NativeActivity;
import android.os.Bundle;

public class ArcadeActivity extends NativeActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    // Called via JNI from native C++ when ROM or core encounters an error
    public void showFatalError(final String title, final String message) {
        runOnUiThread(() -> {
            new android.app.AlertDialog.Builder(this)
                .setTitle(title != null ? title : "Arcade Engine Notice")
                .setMessage(message)
                .setPositiveButton("Return to Menu", (dialog, which) -> finish())
                .setCancelable(false)
                .show();
        });
    }
}
