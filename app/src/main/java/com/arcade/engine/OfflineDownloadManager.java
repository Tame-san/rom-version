package com.arcade.engine;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Manages offline storage, cache lifecycle, filtering, and batch downloading
 * for arcade ROMs.
 */
public class OfflineDownloadManager {

    public enum FilterTab {
        ALL,
        OFFLINE,
        NETWORK
    }

    public interface ItemDownloader {
        void download(String url, File targetDir, String fileName, AtomicBoolean cancelSignal,
                      NetworkRomSource.DownloadProgressListener listener) throws Exception;
    }

    public static final ItemDownloader DEFAULT_DOWNLOADER = (url, targetDir, fileName, cancelSignal, listener) -> {
        NetworkRomSource.downloadRomSync(url, targetDir, fileName, cancelSignal, listener);
    };

    public interface BatchDownloadListener {
        void onQueueStart(int totalCount, long totalBytes);
        void onItemStart(int index, int totalCount, GameItem item);
        void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long totalBytes, double speedBytesPerSec);
        void onItemComplete(int index, int totalCount, GameItem item, File file);
        void onItemError(int index, int totalCount, GameItem item, Exception error);
        void onQueueFinished(int completedCount, int totalCount, boolean cancelled);
    }

    /**
     * Filters games by selected tab and case-insensitive search query.
     */
    public static List<GameItem> filterGames(List<GameItem> items, FilterTab tab, String searchQuery, File romsDir) {
        List<GameItem> result = new ArrayList<>();
        if (items == null) return result;

        String query = searchQuery == null ? "" : searchQuery.trim().toLowerCase(Locale.ROOT);

        for (GameItem item : items) {
            if (item == null) continue;

            boolean tabMatches = false;
            switch (tab) {
                case ALL:
                    tabMatches = true;
                    break;
                case OFFLINE:
                    tabMatches = item.isDownloaded(romsDir);
                    break;
                case NETWORK:
                    tabMatches = item.isNetwork;
                    break;
            }
            if (!tabMatches) continue;

            if (query.isEmpty()) {
                result.add(item);
            } else {
                boolean matchTitle = item.name != null && item.name.toLowerCase(Locale.ROOT).contains(query);
                boolean matchFile = item.fileName != null && item.fileName.toLowerCase(Locale.ROOT).contains(query);
                boolean matchSystem = item.systemTag != null && item.systemTag.toLowerCase(Locale.ROOT).contains(query);
                if (matchTitle || matchFile || matchSystem) {
                    result.add(item);
                }
            }
        }
        return result;
    }

    /**
     * Returns total count of games matching a specific filter tab regardless of search query.
     */
    public static int countGames(List<GameItem> items, FilterTab tab, File romsDir) {
        if (items == null) return 0;
        int count = 0;
        for (GameItem item : items) {
            if (item == null) continue;
            switch (tab) {
                case ALL:
                    count++;
                    break;
                case OFFLINE:
                    if (item.isDownloaded(romsDir)) count++;
                    break;
                case NETWORK:
                    if (item.isNetwork) count++;
                    break;
            }
        }
        return count;
    }

    /**
     * Safely deletes local cached copy of a ROM file and resets item local path.
     * Returns the total number of bytes reclaimed.
     */
    public static long deleteLocalCopy(GameItem item, File romsDir) {
        if (item == null) return 0L;
        long bytesReclaimed = 0L;

        if (item.localPath != null) {
            File localFile = new File(item.localPath);
            if (localFile.exists()) {
                bytesReclaimed += localFile.length();
                try {
                    localFile.delete();
                } catch (Exception ignored) {}
            }
            item.localPath = null;
        }

        if (romsDir != null && item.fileName != null) {
            File dirFile = new File(romsDir, item.fileName);
            if (dirFile.exists()) {
                if (bytesReclaimed == 0) {
                    bytesReclaimed += dirFile.length();
                }
                try {
                    dirFile.delete();
                } catch (Exception ignored) {}
            }
        }

        return bytesReclaimed;
    }

    /**
     * Determines whether a game item should remain in the catalog after local deletion.
     * Network titles are retained so users can re-download them.
     */
    public static boolean shouldRetainAfterDeletion(GameItem item) {
        return item != null && item.isNetwork;
    }

    /**
     * Returns list of network games that have not yet been downloaded to local storage.
     */
    public static List<GameItem> getUndownloadedNetworkGames(List<GameItem> items, File romsDir) {
        List<GameItem> result = new ArrayList<>();
        if (items == null) return result;
        for (GameItem item : items) {
            if (item != null && item.isNetwork && item.remoteUrl != null && !item.isDownloaded(romsDir)) {
                result.add(item);
            }
        }
        return result;
    }

    /**
     * Calculates the estimated total bytes for a list of games.
     */
    public static long calculateTotalDownloadBytes(List<GameItem> items) {
        if (items == null) return 0L;
        long total = 0L;
        for (GameItem item : items) {
            if (item != null && item.sizeBytes > 0) {
                total += item.sizeBytes;
            }
        }
        return total;
    }

    /**
     * Formats byte size into human-readable representation.
     */
    public static String formatBytes(long bytes) {
        if (bytes <= 0) return "0 MB";
        double mb = bytes / (1024.0 * 1024.0);
        if (mb >= 1.0) {
            return String.format(Locale.ROOT, "%.1f MB", mb);
        } else {
            double kb = bytes / 1024.0;
            return String.format(Locale.ROOT, "%.1f KB", kb);
        }
    }

    /**
     * Handles sequential batch downloads of game items with progress and cancellation.
     */
    public static class BatchQueue {
        private final List<GameItem> items;
        private final AtomicBoolean cancelSignal;
        private final ItemDownloader downloader;
        private int completedCount = 0;
        private int failedCount = 0;
        private int currentIndex = 0;

        public BatchQueue(List<GameItem> items) {
            this(items, DEFAULT_DOWNLOADER);
        }

        public BatchQueue(List<GameItem> items, ItemDownloader downloader) {
            this.items = items != null ? new ArrayList<>(items) : new ArrayList<>();
            this.cancelSignal = new AtomicBoolean(false);
            this.downloader = downloader != null ? downloader : DEFAULT_DOWNLOADER;
        }

        public void cancel() {
            cancelSignal.set(true);
        }

        public boolean isCancelled() {
            return cancelSignal.get();
        }

        public int getTotalCount() {
            return items.size();
        }

        public int getCompletedCount() {
            return completedCount;
        }

        public int getFailedCount() {
            return failedCount;
        }

        public int getCurrentIndex() {
            return currentIndex;
        }

        public AtomicBoolean getCancelSignal() {
            return cancelSignal;
        }

        public List<GameItem> getItems() {
            return Collections.unmodifiableList(items);
        }

        public void run(File romsDir, BatchDownloadListener listener) {
            long totalBytes = calculateTotalDownloadBytes(items);
            if (listener != null) {
                listener.onQueueStart(items.size(), totalBytes);
            }

            for (int i = 0; i < items.size(); i++) {
                if (cancelSignal.get()) {
                    break;
                }
                currentIndex = i;
                GameItem currentItem = items.get(i);
                if (listener != null) {
                    listener.onItemStart(i, items.size(), currentItem);
                }

                try {
                    downloader.download(
                            currentItem.remoteUrl,
                            romsDir,
                            currentItem.fileName,
                            cancelSignal,
                            new NetworkRomSource.DownloadProgressListener() {
                                @Override
                                public void onProgress(long bytesRead, long total, double speed) {
                                    if (listener != null) {
                                        listener.onItemProgress(currentIndex, items.size(), currentItem, bytesRead, total, speed);
                                    }
                                }

                                @Override
                                public void onComplete(File file) {
                                    currentItem.localPath = file.getAbsolutePath();
                                    completedCount++;
                                    if (listener != null) {
                                        listener.onItemComplete(currentIndex, items.size(), currentItem, file);
                                    }
                                }

                                @Override
                                public void onError(Exception error) {
                                    failedCount++;
                                    if (listener != null) {
                                        listener.onItemError(currentIndex, items.size(), currentItem, error);
                                    }
                                }
                            }
                    );
                } catch (Exception e) {
                    if (cancelSignal.get()) {
                        break;
                    }
                    failedCount++;
                    if (listener != null) {
                        listener.onItemError(i, items.size(), currentItem, e);
                    }
                }
            }

            if (listener != null) {
                listener.onQueueFinished(completedCount, items.size(), cancelSignal.get());
            }
        }
    }
}
