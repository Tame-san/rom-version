package com.arcade.engine;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class OfflineDownloadManagerTest {

    @Rule
    public TemporaryFolder tempFolder = new TemporaryFolder();

    // -------------------------------------------------------------------------
    // 1. Filter Logic Tests (ALL, OFFLINE, NETWORK + Search Queries)
    // -------------------------------------------------------------------------

    @Test
    public void testFilterGames_AllTab() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File localDownloaded = new File(romsDir, "mslug.zip");
        createDummyFile(localDownloaded, 1024);

        List<GameItem> catalog = new ArrayList<>();
        // 1. Downloaded network game
        catalog.add(new GameItem("Metal Slug", "mslug.zip", null, localDownloaded.getAbsolutePath(),
                "NEO GEO", "http://server/mslug.zip", true, 1024));
        // 2. Remote-only network game (not downloaded)
        catalog.add(new GameItem("Street Fighter II", "sf2.zip", null, null,
                "CPS-1", "http://server/sf2.zip", true, 2048));
        // 3. Local-only offline game (never from network)
        File localOnly = new File(romsDir, "kof98.zip");
        createDummyFile(localOnly, 4096);
        catalog.add(new GameItem("The King of Fighters '98", "kof98.zip", null, localOnly.getAbsolutePath(),
                "NEO GEO", null, false, 4096));

        List<GameItem> filtered = OfflineDownloadManager.filterGames(
                catalog, OfflineDownloadManager.FilterTab.ALL, "", romsDir);

        Assert.assertEquals(3, filtered.size());
    }

    @Test
    public void testFilterGames_OfflineTab() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File localDownloaded = new File(romsDir, "mslug.zip");
        createDummyFile(localDownloaded, 1024);

        List<GameItem> catalog = new ArrayList<>();
        // Downloaded network game (should be in OFFLINE)
        catalog.add(new GameItem("Metal Slug", "mslug.zip", null, localDownloaded.getAbsolutePath(),
                "NEO GEO", "http://server/mslug.zip", true, 1024));
        // Remote-only (should NOT be in OFFLINE)
        catalog.add(new GameItem("Street Fighter II", "sf2.zip", null, null,
                "CPS-1", "http://server/sf2.zip", true, 2048));
        // Purely local file (should be in OFFLINE)
        File localOnly = new File(romsDir, "kof98.zip");
        createDummyFile(localOnly, 4096);
        catalog.add(new GameItem("The King of Fighters '98", "kof98.zip", null, localOnly.getAbsolutePath(),
                "NEO GEO", null, false, 4096));

        List<GameItem> filtered = OfflineDownloadManager.filterGames(
                catalog, OfflineDownloadManager.FilterTab.OFFLINE, "", romsDir);

        Assert.assertEquals(2, filtered.size());
        Assert.assertEquals("mslug.zip", filtered.get(0).fileName);
        Assert.assertEquals("kof98.zip", filtered.get(1).fileName);
    }

    @Test
    public void testFilterGames_NetworkTab() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File localDownloaded = new File(romsDir, "mslug.zip");
        createDummyFile(localDownloaded, 1024);

        List<GameItem> catalog = new ArrayList<>();
        // Downloaded network game (isNetwork == true)
        catalog.add(new GameItem("Metal Slug", "mslug.zip", null, localDownloaded.getAbsolutePath(),
                "NEO GEO", "http://server/mslug.zip", true, 1024));
        // Remote-only network game (isNetwork == true)
        catalog.add(new GameItem("Street Fighter II", "sf2.zip", null, null,
                "CPS-1", "http://server/sf2.zip", true, 2048));
        // Purely local offline file (isNetwork == false)
        File localOnly = new File(romsDir, "kof98.zip");
        createDummyFile(localOnly, 4096);
        catalog.add(new GameItem("The King of Fighters '98", "kof98.zip", null, localOnly.getAbsolutePath(),
                "NEO GEO", null, false, 4096));

        List<GameItem> filtered = OfflineDownloadManager.filterGames(
                catalog, OfflineDownloadManager.FilterTab.NETWORK, "", romsDir);

        Assert.assertEquals(2, filtered.size());
        Assert.assertEquals("mslug.zip", filtered.get(0).fileName);
        Assert.assertEquals("sf2.zip", filtered.get(1).fileName);
    }

    @Test
    public void testFilterGames_SearchMatchingTitleFileNameAndSystem() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        List<GameItem> catalog = new ArrayList<>();
        catalog.add(new GameItem("Metal Slug 2", "mslug2.zip", null, null, "NEO GEO", "http://s/mslug2.zip", true, 100));
        catalog.add(new GameItem("Garou: Mark of the Wolves", "garou.zip", null, null, "NEO GEO", "http://s/garou.zip", true, 200));
        catalog.add(new GameItem("Street Fighter Alpha 3", "sfa3.zip", null, null, "CPS-2", "http://s/sfa3.zip", true, 300));

        // Match title
        List<GameItem> matchTitle = OfflineDownloadManager.filterGames(
                catalog, OfflineDownloadManager.FilterTab.ALL, "mark of", romsDir);
        Assert.assertEquals(1, matchTitle.size());
        Assert.assertEquals("garou.zip", matchTitle.get(0).fileName);

        // Match fileName
        List<GameItem> matchFile = OfflineDownloadManager.filterGames(
                catalog, OfflineDownloadManager.FilterTab.ALL, "mslug2", romsDir);
        Assert.assertEquals(1, matchFile.size());
        Assert.assertEquals("Metal Slug 2", matchFile.get(0).name);

        // Match system
        List<GameItem> matchSystem = OfflineDownloadManager.filterGames(
                catalog, OfflineDownloadManager.FilterTab.ALL, "cps-2", romsDir);
        Assert.assertEquals(1, matchSystem.size());
        Assert.assertEquals("sfa3.zip", matchSystem.get(0).fileName);
    }

    @Test
    public void testCountGames() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File downloaded = new File(romsDir, "dino.zip");
        createDummyFile(downloaded, 512);

        List<GameItem> catalog = new ArrayList<>();
        catalog.add(new GameItem("Cadillacs and Dinosaurs", "dino.zip", null, downloaded.getAbsolutePath(), "CPS-1", "http://s/dino.zip", true, 512));
        catalog.add(new GameItem("The Punisher", "punisher.zip", null, null, "CPS-1", "http://s/punisher.zip", true, 1024));
        catalog.add(new GameItem("Local Game", "local.zip", null, downloaded.getAbsolutePath(), "ARCADE", null, false, 512));

        Assert.assertEquals(3, OfflineDownloadManager.countGames(catalog, OfflineDownloadManager.FilterTab.ALL, romsDir));
        Assert.assertEquals(2, OfflineDownloadManager.countGames(catalog, OfflineDownloadManager.FilterTab.OFFLINE, romsDir));
        Assert.assertEquals(2, OfflineDownloadManager.countGames(catalog, OfflineDownloadManager.FilterTab.NETWORK, romsDir));
    }

    // -------------------------------------------------------------------------
    // 2. Local File Deletion & Cache State Tests
    // -------------------------------------------------------------------------

    @Test
    public void testDeleteLocalCopy_DeletesFileAndClearsLocalPath() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File romFile = new File(romsDir, "mslugx.zip");
        createDummyFile(romFile, 5000);
        Assert.assertTrue(romFile.exists());

        GameItem item = new GameItem("Metal Slug X", "mslugx.zip", null, romFile.getAbsolutePath(),
                "NEO GEO", "http://server/mslugx.zip", true, 5000);

        Assert.assertTrue(item.isDownloaded(romsDir));

        long reclaimed = OfflineDownloadManager.deleteLocalCopy(item, romsDir);

        Assert.assertEquals(5000L, reclaimed);
        Assert.assertNull(item.localPath);
        Assert.assertFalse(romFile.exists());
        Assert.assertFalse(item.isDownloaded(romsDir));
    }

    @Test
    public void testDeleteLocalCopy_DeletesFileFromRomsDirWhenLocalPathNull() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File romFile = new File(romsDir, "wjammers.zip");
        createDummyFile(romFile, 8000);

        GameItem item = new GameItem("Windjammers", "wjammers.zip", null, null,
                "NEO GEO", "http://server/wjammers.zip", true, 8000);

        Assert.assertTrue(item.isDownloaded(romsDir));

        long reclaimed = OfflineDownloadManager.deleteLocalCopy(item, romsDir);

        Assert.assertEquals(8000L, reclaimed);
        Assert.assertFalse(romFile.exists());
        Assert.assertFalse(item.isDownloaded(romsDir));
    }

    @Test
    public void testShouldRetainAfterDeletion() {
        GameItem networkItem = new GameItem("Game", "game.zip", null, null, "ARCADE", "http://s/game.zip", true, 100);
        GameItem localItem = new GameItem("Game", "game.zip", null, null, "ARCADE", null, false, 100);

        Assert.assertTrue(OfflineDownloadManager.shouldRetainAfterDeletion(networkItem));
        Assert.assertFalse(OfflineDownloadManager.shouldRetainAfterDeletion(localItem));
        Assert.assertFalse(OfflineDownloadManager.shouldRetainAfterDeletion(null));
    }

    @Test
    public void testFormatBytes() {
        Assert.assertEquals("0 MB", OfflineDownloadManager.formatBytes(0));
        Assert.assertEquals("0 MB", OfflineDownloadManager.formatBytes(-50));
        Assert.assertEquals("500.0 KB", OfflineDownloadManager.formatBytes(512000));
        Assert.assertEquals("10.0 MB", OfflineDownloadManager.formatBytes(10485760));
    }

    // -------------------------------------------------------------------------
    // 3. Batch Queue & Download Planning Tests
    // -------------------------------------------------------------------------

    @Test
    public void testGetUndownloadedNetworkGamesAndTotalBytes() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File savedRom = new File(romsDir, "mslug.zip");
        createDummyFile(savedRom, 10_000_000);

        List<GameItem> catalog = new ArrayList<>();
        // 1. Downloaded network game (should be ignored)
        catalog.add(new GameItem("Metal Slug", "mslug.zip", null, savedRom.getAbsolutePath(),
                "NEO GEO", "http://server/mslug.zip", true, 10_000_000));
        // 2. Un-downloaded network game 1
        catalog.add(new GameItem("Street Fighter II", "sf2.zip", null, null,
                "CPS-1", "http://server/sf2.zip", true, 20_000_000));
        // 3. Un-downloaded network game 2
        catalog.add(new GameItem("Garou", "garou.zip", null, null,
                "NEO GEO", "http://server/garou.zip", true, 30_000_000));
        // 4. Local-only game (no remote URL, should be ignored)
        catalog.add(new GameItem("Local", "local.zip", null, savedRom.getAbsolutePath(),
                "ARCADE", null, false, 5_000_000));

        List<GameItem> queueItems = OfflineDownloadManager.getUndownloadedNetworkGames(catalog, romsDir);

        Assert.assertEquals(2, queueItems.size());
        Assert.assertEquals("sf2.zip", queueItems.get(0).fileName);
        Assert.assertEquals("garou.zip", queueItems.get(1).fileName);

        long totalBytes = OfflineDownloadManager.calculateTotalDownloadBytes(queueItems);
        Assert.assertEquals(50_000_000L, totalBytes);
    }

    @Test
    public void testBatchQueue_SequentialExecution() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        List<GameItem> queueItems = new ArrayList<>();
        queueItems.add(new GameItem("Game 1", "game1.zip", null, null, "ARCADE", "http://s/1.zip", true, 1000));
        queueItems.add(new GameItem("Game 2", "game2.zip", null, null, "ARCADE", "http://s/2.zip", true, 2000));
        queueItems.add(new GameItem("Game 3", "game3.zip", null, null, "ARCADE", "http://s/3.zip", true, 3000));

        AtomicInteger itemsStarted = new AtomicInteger(0);
        AtomicInteger itemsCompleted = new AtomicInteger(0);
        AtomicBoolean queueFinished = new AtomicBoolean(false);

        // Mock ItemDownloader creates dummy downloaded file
        OfflineDownloadManager.ItemDownloader mockDownloader = (url, targetDir, fileName, cancelSignal, listener) -> {
            File outFile = new File(targetDir, fileName);
            createDummyFile(outFile, 100);
            if (listener != null) {
                listener.onProgress(100, 100, 1000);
                listener.onComplete(outFile);
            }
        };

        OfflineDownloadManager.BatchQueue queue = new OfflineDownloadManager.BatchQueue(queueItems, mockDownloader);

        queue.run(romsDir, new OfflineDownloadManager.BatchDownloadListener() {
            @Override
            public void onQueueStart(int totalCount, long totalBytes) {
                Assert.assertEquals(3, totalCount);
                Assert.assertEquals(6000L, totalBytes);
            }

            @Override
            public void onItemStart(int index, int totalCount, GameItem item) {
                itemsStarted.incrementAndGet();
            }

            @Override
            public void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long totalBytes, double speedBytesPerSec) {}

            @Override
            public void onItemComplete(int index, int totalCount, GameItem item, File file) {
                itemsCompleted.incrementAndGet();
                Assert.assertNotNull(item.localPath);
                Assert.assertTrue(new File(item.localPath).exists());
            }

            @Override
            public void onItemError(int index, int totalCount, GameItem item, Exception error) {
                Assert.fail("Item error should not be invoked in successful test");
            }

            @Override
            public void onQueueFinished(int completedCount, int totalCount, boolean cancelled) {
                Assert.assertEquals(3, completedCount);
                Assert.assertEquals(3, totalCount);
                Assert.assertFalse(cancelled);
                queueFinished.set(true);
            }
        });

        Assert.assertEquals(3, itemsStarted.get());
        Assert.assertEquals(3, itemsCompleted.get());
        Assert.assertTrue(queueFinished.get());
        Assert.assertEquals(3, queue.getCompletedCount());
        Assert.assertEquals(0, queue.getFailedCount());
        Assert.assertFalse(queue.isCancelled());
    }

    @Test
    public void testBatchQueue_Cancellation() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        List<GameItem> queueItems = new ArrayList<>();
        queueItems.add(new GameItem("Game 1", "game1.zip", null, null, "ARCADE", "http://s/1.zip", true, 1000));
        queueItems.add(new GameItem("Game 2", "game2.zip", null, null, "ARCADE", "http://s/2.zip", true, 2000));
        queueItems.add(new GameItem("Game 3", "game3.zip", null, null, "ARCADE", "http://s/3.zip", true, 3000));

        OfflineDownloadManager.BatchQueue[] queueRef = new OfflineDownloadManager.BatchQueue[1];

        OfflineDownloadManager.ItemDownloader cancelAfterFirstDownloader = (url, targetDir, fileName, cancelSignal, listener) -> {
            File outFile = new File(targetDir, fileName);
            createDummyFile(outFile, 100);
            if (listener != null) {
                listener.onComplete(outFile);
            }
            if (fileName.equals("game1.zip")) {
                queueRef[0].cancel();
            }
        };

        OfflineDownloadManager.BatchQueue queue = new OfflineDownloadManager.BatchQueue(queueItems, cancelAfterFirstDownloader);
        queueRef[0] = queue;

        AtomicBoolean queueFinished = new AtomicBoolean(false);

        queue.run(romsDir, new OfflineDownloadManager.BatchDownloadListener() {
            @Override
            public void onQueueStart(int totalCount, long totalBytes) {}

            @Override
            public void onItemStart(int index, int totalCount, GameItem item) {}

            @Override
            public void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long totalBytes, double speedBytesPerSec) {}

            @Override
            public void onItemComplete(int index, int totalCount, GameItem item, File file) {}

            @Override
            public void onItemError(int index, int totalCount, GameItem item, Exception error) {}

            @Override
            public void onQueueFinished(int completedCount, int totalCount, boolean cancelled) {
                Assert.assertEquals(1, completedCount);
                Assert.assertEquals(3, totalCount);
                Assert.assertTrue(cancelled);
                queueFinished.set(true);
            }
        });

        Assert.assertTrue(queueFinished.get());
        Assert.assertTrue(queue.isCancelled());
        Assert.assertEquals(1, queue.getCompletedCount());
    }

    @Test
    public void testBatchQueue_ErrorResilienceContinuesNextItem() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        List<GameItem> queueItems = new ArrayList<>();
        queueItems.add(new GameItem("Game 1", "game1.zip", null, null, "ARCADE", "http://s/1.zip", true, 1000));
        queueItems.add(new GameItem("Failing Game 2", "game2.zip", null, null, "ARCADE", "http://s/2.zip", true, 2000));
        queueItems.add(new GameItem("Game 3", "game3.zip", null, null, "ARCADE", "http://s/3.zip", true, 3000));

        OfflineDownloadManager.ItemDownloader downloaderWithFault = (url, targetDir, fileName, cancelSignal, listener) -> {
            if (fileName.equals("game2.zip")) {
                throw new IOException("Simulated network timeout for item 2");
            }
            File outFile = new File(targetDir, fileName);
            createDummyFile(outFile, 100);
            if (listener != null) {
                listener.onComplete(outFile);
            }
        };

        OfflineDownloadManager.BatchQueue queue = new OfflineDownloadManager.BatchQueue(queueItems, downloaderWithFault);
        AtomicInteger errorCount = new AtomicInteger(0);

        queue.run(romsDir, new OfflineDownloadManager.BatchDownloadListener() {
            @Override
            public void onQueueStart(int totalCount, long totalBytes) {}

            @Override
            public void onItemStart(int index, int totalCount, GameItem item) {}

            @Override
            public void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long totalBytes, double speedBytesPerSec) {}

            @Override
            public void onItemComplete(int index, int totalCount, GameItem item, File file) {}

            @Override
            public void onItemError(int index, int totalCount, GameItem item, Exception error) {
                errorCount.incrementAndGet();
            }

            @Override
            public void onQueueFinished(int completedCount, int totalCount, boolean cancelled) {
                Assert.assertEquals(2, completedCount);
                Assert.assertFalse(cancelled);
            }
        });

        Assert.assertEquals(1, errorCount.get());
        Assert.assertEquals(2, queue.getCompletedCount());
        Assert.assertEquals(1, queue.getFailedCount());
    }

    @Test
    public void testDeleteLocalCopy_PreventsPathTraversal_OutsideRomsDir() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        File outsideDir = tempFolder.newFolder("outside");
        File sensitiveFile = new File(outsideDir, "sensitive.txt");
        createDummyFile(sensitiveFile, 1234);
        Assert.assertTrue(sensitiveFile.exists());

        // Attempt 1: localPath points outside romsDir
        GameItem maliciousItem1 = new GameItem("Malicious 1", "innocent.zip", null,
                sensitiveFile.getAbsolutePath(), "ARCADE", "http://s/1.zip", true, 1234);
        long reclaimed1 = OfflineDownloadManager.deleteLocalCopy(maliciousItem1, romsDir);

        Assert.assertEquals(0L, reclaimed1);
        Assert.assertTrue("Sensitive file outside romsDir must NOT be deleted", sensitiveFile.exists());
        Assert.assertNull(maliciousItem1.localPath);

        // Attempt 2: fileName has directory traversal characters
        GameItem maliciousItem2 = new GameItem("Malicious 2", "../../outside/sensitive.txt", null,
                null, "ARCADE", "http://s/2.zip", true, 1234);
        long reclaimed2 = OfflineDownloadManager.deleteLocalCopy(maliciousItem2, romsDir);

        Assert.assertEquals(0L, reclaimed2);
        Assert.assertTrue("Sensitive file outside romsDir must NOT be deleted via fileName traversal", sensitiveFile.exists());
    }

    @Test
    public void testDeleteLocalCopy_NullRomsDir() {
        GameItem item = new GameItem("Game", "game.zip", null, "/tmp/some_game.zip", "ARCADE", "http://s/g.zip", true, 500);
        long reclaimed = OfflineDownloadManager.deleteLocalCopy(item, null);
        Assert.assertEquals(0L, reclaimed);
    }

    @Test
    public void testBatchQueue_ErrorCounterNoDoubleCount() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        List<GameItem> queueItems = new ArrayList<>();
        queueItems.add(new GameItem("Error Game", "err.zip", null, null, "ARCADE", "http://s/err.zip", true, 1000));

        // Downloader both invokes listener.onError AND throws Exception
        OfflineDownloadManager.ItemDownloader doubleErrorDownloader = (url, targetDir, fileName, cancelSignal, listener) -> {
            IOException ioEx = new IOException("Connection failed");
            if (listener != null) {
                listener.onError(ioEx);
            }
            throw ioEx;
        };

        OfflineDownloadManager.BatchQueue queue = new OfflineDownloadManager.BatchQueue(queueItems, doubleErrorDownloader);
        AtomicInteger onErrorCalls = new AtomicInteger(0);

        queue.run(romsDir, new OfflineDownloadManager.BatchDownloadListener() {
            @Override
            public void onQueueStart(int totalCount, long totalBytes) {}

            @Override
            public void onItemStart(int index, int totalCount, GameItem item) {}

            @Override
            public void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long totalBytes, double speedBytesPerSec) {}

            @Override
            public void onItemComplete(int index, int totalCount, GameItem item, File file) {}

            @Override
            public void onItemError(int index, int totalCount, GameItem item, Exception error) {
                onErrorCalls.incrementAndGet();
            }

            @Override
            public void onQueueFinished(int completedCount, int totalCount, boolean cancelled) {
                Assert.assertEquals(0, completedCount);
                Assert.assertFalse(cancelled);
            }
        });

        // Must be exactly 1 error call and 1 failed count, not 2
        Assert.assertEquals(1, onErrorCalls.get());
        Assert.assertEquals(1, queue.getFailedCount());
        Assert.assertEquals(0, queue.getCompletedCount());
    }

    @Test
    public void testBatchQueue_CancelDoesNotIncrementFailedCount() throws IOException {
        File romsDir = tempFolder.newFolder("roms");
        List<GameItem> queueItems = new ArrayList<>();
        queueItems.add(new GameItem("Game 1", "game1.zip", null, null, "ARCADE", "http://s/1.zip", true, 1000));
        queueItems.add(new GameItem("Game 2", "game2.zip", null, null, "ARCADE", "http://s/2.zip", true, 2000));

        // Downloader triggers cancellation and throws CancellationException
        OfflineDownloadManager.ItemDownloader cancelDownloader = (url, targetDir, fileName, cancelSignal, listener) -> {
            cancelSignal.set(true);
            throw new java.util.concurrent.CancellationException("User cancelled");
        };

        OfflineDownloadManager.BatchQueue queue = new OfflineDownloadManager.BatchQueue(queueItems, cancelDownloader);
        AtomicInteger onErrorCalls = new AtomicInteger(0);

        queue.run(romsDir, new OfflineDownloadManager.BatchDownloadListener() {
            @Override
            public void onQueueStart(int totalCount, long totalBytes) {}

            @Override
            public void onItemStart(int index, int totalCount, GameItem item) {}

            @Override
            public void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long totalBytes, double speedBytesPerSec) {}

            @Override
            public void onItemComplete(int index, int totalCount, GameItem item, File file) {}

            @Override
            public void onItemError(int index, int totalCount, GameItem item, Exception error) {
                onErrorCalls.incrementAndGet();
            }

            @Override
            public void onQueueFinished(int completedCount, int totalCount, boolean cancelled) {
                Assert.assertTrue(cancelled);
            }
        });

        Assert.assertEquals(0, onErrorCalls.get());
        Assert.assertEquals(0, queue.getFailedCount());
        Assert.assertTrue(queue.isCancelled());
    }

    private static void createDummyFile(File file, int size) throws IOException {
        if (!file.getParentFile().exists()) {
            file.getParentFile().mkdirs();
        }
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(new byte[size]);
        }
    }
}
