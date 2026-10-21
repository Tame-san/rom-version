package com.arcade.engine;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends Activity {

    private static final int REQ_SELECT_ROM = 1001;
    private static final int REQ_SELECT_FOLDER = 1002;

    private TextView tvBiosStatus;
    private TextView tvRomStatus;
    private Button btnSelectRom;
    private Button btnSelectFolder;
    private Button btnQuickMetalSlug;
    private ListView lvGames;

    private List<GameItem> gameList = new ArrayList<>();
    private ArrayAdapter<String> listAdapter;
    private List<String> displayNames = new ArrayList<>();

    private static class GameItem {
        String name;
        String fileName;
        Uri uri;
        String localPath;

        GameItem(String name, String fileName, Uri uri, String localPath) {
            this.name = name;
            this.fileName = fileName;
            this.uri = uri;
            this.localPath = localPath;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvBiosStatus = findViewById(R.id.tv_bios_status);
        tvRomStatus = findViewById(R.id.tv_rom_status);
        btnSelectRom = findViewById(R.id.btn_select_rom);
        btnSelectFolder = findViewById(R.id.btn_select_folder);
        btnQuickMetalSlug = findViewById(R.id.btn_quick_metal_slug);
        lvGames = findViewById(R.id.lv_games);

        listAdapter = new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, displayNames);
        lvGames.setAdapter(listAdapter);

        btnSelectRom.setOnClickListener(v -> openFilePicker());
        btnSelectFolder.setOnClickListener(v -> openFolderPicker());
        btnQuickMetalSlug.setOnClickListener(v -> quickLaunchMetalSlug());

        lvGames.setOnItemClickListener((parent, view, position, id) -> {
            if (position >= 0 && position < gameList.size()) {
                launchGameItem(gameList.get(position));
            }
        });

        // Extract bundled BIOS files from APK assets (neogeo.zip, pgm.zip, etc.)
        extractBundledBioses();

        // Initial scan of internal app storage and check BIOS
        updateBiosStatus();
        scanInternalRoms();
        scanCommonPhonePaths();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ensureBiosSynchronized();
        updateBiosStatus();
        scanInternalRoms();
    }

    private File getRomsDir() {
        File dir = new File(getExternalFilesDir(null), "roms");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    private File getBiosDir() {
        File dir = new File(getExternalFilesDir(null), "bios");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    private File getSystemDir() {
        File dir = new File(getExternalFilesDir(null), "system");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    private File getSavesDir() {
        File dir = new File(getExternalFilesDir(null), "saves");
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    private void extractBundledBioses() {
        new Thread(() -> {
            try {
                String[] bioses = getAssets().list("bios");
                if (bioses == null || bioses.length == 0) return;

                File romsDir = getRomsDir();
                File biosDir = getBiosDir();
                File systemDir = getSystemDir();
                getSavesDir(); // Ensure saves directory exists

                for (String bios : bioses) {
                    if (!bios.toLowerCase().endsWith(".zip")) continue;
                    File targetRoms = new File(romsDir, bios);
                    File targetBios = new File(biosDir, bios);
                    File targetSystem = new File(systemDir, bios);

                    if (!targetRoms.exists() || targetRoms.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetRoms);
                    }
                    if (!targetBios.exists() || targetBios.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetBios);
                    }
                    if (!targetSystem.exists() || targetSystem.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetSystem);
                    }
                }

                runOnUiThread(() -> {
                    updateBiosStatus();
                    scanInternalRoms();
                });
            } catch (Exception e) {
                android.util.Log.e("MainActivity", "Failed to extract bundled BIOSes: " + e.getMessage());
            }
        }).start();
    }

    private void copyAssetToFile(String assetPath, File dest) throws Exception {
        try (InputStream in = getAssets().open(assetPath);
             OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[65536];
            int len;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
            }
        }
    }

    private void ensureBiosSynchronized() {
        File src = new File(getBiosDir(), "neogeo.zip");
        File dst = new File(getRomsDir(), "neogeo.zip");
        if (src.exists() && (!dst.exists() || dst.length() == 0)) {
            try {
                java.nio.file.Files.copy(src.toPath(), dst.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } catch (Exception ignored) {}
        }
        File sys = new File(getSystemDir(), "neogeo.zip");
        if (src.exists() && (!sys.exists() || sys.length() == 0)) {
            try {
                java.nio.file.Files.copy(src.toPath(), sys.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } catch (Exception ignored) {}
        }
    }

    private boolean isBiosPresent() {
        File neogeoRoms = new File(getRomsDir(), "neogeo.zip");
        File neogeoBios = new File(getBiosDir(), "neogeo.zip");
        File neogeoSys = new File(getSystemDir(), "neogeo.zip");
        return (neogeoRoms.exists() && neogeoRoms.length() > 1000) ||
               (neogeoBios.exists() && neogeoBios.length() > 1000) ||
               (neogeoSys.exists() && neogeoSys.length() > 1000);
    }

    private boolean isNeoGeoRom(String fileName) {
        if (fileName == null) return false;
        String base = fileName.toLowerCase().replace(".zip", "");
        return base.startsWith("mslug") || base.startsWith("kof") || base.startsWith("garou") ||
               base.startsWith("lastbld") || base.startsWith("samsho") || base.startsWith("rbff") ||
               base.startsWith("sengoku") || base.startsWith("wjammers") || base.startsWith("spinmast") ||
               base.startsWith("nam1975") || base.startsWith("fatfur") || base.startsWith("aof") ||
               base.startsWith("neogal");
    }

    private void updateBiosStatus() {
        ensureBiosSynchronized();
        if (isBiosPresent()) {
            tvBiosStatus.setText("✓ Neo Geo BIOS: Ready (neogeo.zip active)");
            tvBiosStatus.setTextColor(0xFF00E676); // Bright green
        } else {
            tvBiosStatus.setText("⚠ Neo Geo BIOS: Not found (Tap to install neogeo.zip)");
            tvBiosStatus.setTextColor(0xFFFFB300); // Amber
            tvBiosStatus.setOnClickListener(v -> openFilePicker());
        }
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQ_SELECT_ROM);
    }

    private void openFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQ_SELECT_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) return;

        if (requestCode == REQ_SELECT_ROM) {
            Uri uri = data.getData();
            if (uri != null) {
                handleSelectedFileUri(uri);
            }
        } else if (requestCode == REQ_SELECT_FOLDER) {
            Uri treeUri = data.getData();
            if (treeUri != null) {
                try {
                    getContentResolver().takePersistableUriPermission(
                            treeUri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                } catch (Exception ignored) {}
                scanDocumentTree(treeUri);
            }
        }
    }

    private String getFileNameFromUri(Uri uri) {
        DocumentFile doc = DocumentFile.fromSingleUri(this, uri);
        if (doc != null && doc.getName() != null) {
            return doc.getName();
        }
        String path = uri.getPath();
        if (path != null) {
            int idx = path.lastIndexOf('/');
            if (idx != -1) return path.substring(idx + 1);
        }
        return "game.zip";
    }

    private void handleSelectedFileUri(Uri uri) {
        String fileName = getFileNameFromUri(uri);
        Toast.makeText(this, "Loading " + fileName + "...", Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            try {
                File target = new File(getRomsDir(), fileName);
                copyUriToFile(uri, target);

                // If user selected neogeo.zip, update BIOS status
                if (fileName.equalsIgnoreCase("neogeo.zip")) {
                    runOnUiThread(() -> {
                        updateBiosStatus();
                        Toast.makeText(MainActivity.this, "Neo Geo BIOS installed successfully!", Toast.LENGTH_SHORT).show();
                    });
                    return;
                }

                runOnUiThread(() -> {
                    updateBiosStatus();
                    launchDirect(target.getAbsolutePath());
                });
            } catch (Exception e) {
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Error copying ROM: " + e.getMessage(), Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    private void copyUriToFile(Uri uri, File dest) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[65536];
            int len;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
            }
        }
    }

    private void scanDocumentTree(Uri treeUri) {
        Toast.makeText(this, "Scanning ROMs folder...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            DocumentFile root = DocumentFile.fromTreeUri(this, treeUri);
            if (root == null || !root.isDirectory()) return;

            DocumentFile[] files = root.listFiles();
            List<DocumentFile> zipFiles = new ArrayList<>();

            for (DocumentFile f : files) {
                String name = f.getName();
                if (name != null && name.toLowerCase().endsWith(".zip")) {
                    // Check for neogeo.zip and copy it to app roms folder
                    if (name.equalsIgnoreCase("neogeo.zip")) {
                        try {
                            copyUriToFile(f.getUri(), new File(getRomsDir(), "neogeo.zip"));
                        } catch (Exception ignored) {}
                    }
                    zipFiles.add(f);
                }
            }

            runOnUiThread(() -> {
                gameList.clear();
                displayNames.clear();

                for (DocumentFile f : zipFiles) {
                    String name = f.getName();
                    String friendly = formatFriendlyName(name);
                    gameList.add(new GameItem(friendly, name, f.getUri(), null));
                    displayNames.add(friendly);
                }

                listAdapter.notifyDataSetChanged();
                updateBiosStatus();
                tvRomStatus.setText("Found " + gameList.size() + " arcade game(s) in selected folder");
            });
        }).start();
    }

    private void scanInternalRoms() {
        File[] files = getRomsDir().listFiles((dir, name) -> name.toLowerCase().endsWith(".zip"));
        if (files == null) return;

        for (File f : files) {
            String name = f.getName();
            if (name.equalsIgnoreCase("neogeo.zip")) continue;
            String friendly = formatFriendlyName(name);
            boolean exists = false;
            for (GameItem item : gameList) {
                if (item.fileName.equalsIgnoreCase(name)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                gameList.add(new GameItem(friendly, name, null, f.getAbsolutePath()));
                displayNames.add(friendly);
            }
        }
        listAdapter.notifyDataSetChanged();
    }

    private void scanCommonPhonePaths() {
        String[] paths = {
            "/sdcard/roms/mslug.zip",
            "/storage/emulated/0/roms/mslug.zip",
            "/sdcard/Download/roms/mslug.zip",
            "/storage/emulated/0/Download/roms/mslug.zip",
            "/sdcard/Download/mslug.zip",
            "/storage/emulated/0/Download/mslug.zip"
        };
        for (String p : paths) {
            File f = new File(p);
            if (f.exists() && f.canRead()) {
                String friendly = "Metal Slug (Phone: " + f.getParent() + ")";
                gameList.add(new GameItem(friendly, "mslug.zip", null, f.getAbsolutePath()));
                displayNames.add(friendly);
                listAdapter.notifyDataSetChanged();
                break;
            }
        }
    }

    private String formatFriendlyName(String fileName) {
        String base = fileName.toLowerCase().replace(".zip", "");
        switch (base) {
            case "mslug": return "Metal Slug - Super Vehicle-001 (Neo Geo)";
            case "mslug2": return "Metal Slug 2 (Neo Geo)";
            case "mslugx": return "Metal Slug X (Neo Geo)";
            case "mslug5": return "Metal Slug 5 (Neo Geo)";
            case "garou": return "Garou: Mark of the Wolves (Neo Geo)";
            case "kof98": return "The King of Fighters '98 (Neo Geo)";
            case "kof2002": return "The King of Fighters 2002 (Neo Geo)";
            case "lastbld2": return "The Last Blade 2 (Neo Geo)";
            case "samsho4": return "Samurai Shodown IV (Neo Geo)";
            case "sf2": return "Street Fighter II: The World Warrior (CPS1)";
            case "sfa2": return "Street Fighter Alpha 2 (CPS2)";
            case "dino": return "Cadillacs and Dinosaurs (CPS1)";
            case "punisher": return "The Punisher (CPS1)";
            case "martmast": return "Martial Masters (PGM)";
            default: return fileName;
        }
    }

    private void launchGameItem(GameItem item) {
        if (item.localPath != null && new File(item.localPath).exists()) {
            launchDirect(item.localPath);
            return;
        }
        if (item.uri != null) {
            Toast.makeText(this, "Preparing " + item.fileName + "...", Toast.LENGTH_SHORT).show();
            new Thread(() -> {
                try {
                    File target = new File(getRomsDir(), item.fileName);
                    if (!target.exists() || target.length() == 0) {
                        copyUriToFile(item.uri, target);
                    }
                    runOnUiThread(() -> launchDirect(target.getAbsolutePath()));
                } catch (Exception e) {
                    runOnUiThread(() -> Toast.makeText(MainActivity.this, "Error: " + e.getMessage(), Toast.LENGTH_LONG).show());
                }
            }).start();
        }
    }

    private void quickLaunchMetalSlug() {
        File localMslug = new File(getRomsDir(), "mslug.zip");
        if (localMslug.exists() && localMslug.length() > 1000) {
            launchDirect(localMslug.getAbsolutePath());
            return;
        }

        // Try direct phone paths
        String[] candidates = {
            "/sdcard/roms/mslug.zip",
            "/storage/emulated/0/roms/mslug.zip",
            "/sdcard/Download/roms/mslug.zip",
            "/storage/emulated/0/Download/roms/mslug.zip",
            "/sdcard/Download/mslug.zip",
            "/storage/emulated/0/Download/mslug.zip"
        };
        for (String c : candidates) {
            File f = new File(c);
            if (f.exists() && f.canRead()) {
                launchDirect(f.getAbsolutePath());
                return;
            }
        }

        Toast.makeText(this, "mslug.zip not found yet. Please tap 'Select ROM File' or 'Select ROMs Folder'!", Toast.LENGTH_LONG).show();
        openFilePicker();
    }

    private void launchDirect(String romPath) {
        ensureBiosSynchronized();

        String fileName = new File(romPath).getName();
        if (isNeoGeoRom(fileName) && !isBiosPresent()) {
            new android.app.AlertDialog.Builder(this)
                .setTitle("Neo Geo BIOS Required")
                .setMessage("'" + fileName + "' requires the Neo Geo BIOS (neogeo.zip) to boot.\n\nPlease place 'neogeo.zip' in your ROMs folder, or select 'neogeo.zip' via the file picker.")
                .setPositiveButton("Select neogeo.zip", (d, w) -> openFilePicker())
                .setNeutralButton("Select Folder", (d, w) -> openFolderPicker())
                .setNegativeButton("Cancel", null)
                .show();
            return;
        }

        Intent intent = new Intent(this, ArcadeActivity.class);
        intent.putExtra("ROM_PATH", romPath);
        intent.putExtra("BIOS_DIR", getBiosDir().getAbsolutePath());
        intent.putExtra("SYSTEM_DIR", getRomsDir().getAbsolutePath());
        intent.putExtra("SAVE_DIR", getSavesDir().getAbsolutePath());
        startActivity(intent);
    }
}
