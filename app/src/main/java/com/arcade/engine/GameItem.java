package com.arcade.engine;

import android.net.Uri;

import java.io.File;

/**
 * Represents a game title in the arcade browser, either local, picked via SAF,
 * or discovered from a network ROM source.
 */
public class GameItem {
    public String name;
    public String fileName;
    public Uri uri;
    public String localPath;
    public String systemTag;
    public String remoteUrl;
    public boolean isNetwork;
    public long sizeBytes;

    public GameItem(String name, String fileName, Uri uri, String localPath, String systemTag) {
        this(name, fileName, uri, localPath, systemTag, null, false, -1);
    }

    public GameItem(String name, String fileName, Uri uri, String localPath, String systemTag,
                    String remoteUrl, boolean isNetwork, long sizeBytes) {
        this.name = name;
        this.fileName = fileName;
        this.uri = uri;
        this.localPath = localPath;
        this.systemTag = systemTag;
        this.remoteUrl = remoteUrl;
        this.isNetwork = isNetwork;
        this.sizeBytes = sizeBytes;
    }

    /**
     * Checks if a valid local copy of this game exists on disk or as a content URI.
     */
    public boolean isDownloaded(File romsDir) {
        if (localPath != null) {
            File f = new File(localPath);
            if (f.exists() && f.length() > 0) return true;
        }
        if (uri != null) return true;
        if (romsDir != null && fileName != null) {
            File f = new File(romsDir, fileName);
            return f.exists() && f.length() > 0;
        }
        return false;
    }
}
