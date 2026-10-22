package com.arcade.engine;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class MainActivity extends Activity {

    private static final int REQ_SELECT_ROM = 1001;
    private static final int REQ_SELECT_FOLDER = 1002;

    private TextView tvBiosStatus;
    private TextView tvRomStatus;
    private Button btnSelectRom;
    private Button btnSelectFolder;
    private EditText etSearch;
    private ListView lvGames;

    private final List<GameItem> masterList = new ArrayList<>();
    private final List<GameItem> displayList = new ArrayList<>();
    private GameAdapter gameAdapter;
    private String currentSearchQuery = "";

    private static class GameItem {
        String name;
        String fileName;
        Uri uri;
        String localPath;
        String systemTag;

        GameItem(String name, String fileName, Uri uri, String localPath, String systemTag) {
            this.name = name;
            this.fileName = fileName;
            this.uri = uri;
            this.localPath = localPath;
            this.systemTag = systemTag;
        }
    }

    private class GameAdapter extends BaseAdapter {
        private final LayoutInflater inflater;

        GameAdapter() {
            this.inflater = LayoutInflater.from(MainActivity.this);
        }

        @Override
        public int getCount() {
            return displayList.size();
        }

        @Override
        public GameItem getItem(int position) {
            return displayList.get(position);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            ViewHolder holder;
            if (convertView == null) {
                convertView = inflater.inflate(R.layout.item_game, parent, false);
                holder = new ViewHolder();
                holder.title = convertView.findViewById(R.id.tv_game_title);
                holder.system = convertView.findViewById(R.id.tv_game_system);
                holder.filename = convertView.findViewById(R.id.tv_game_filename);
                holder.playBtn = convertView.findViewById(R.id.tv_play_btn);
                convertView.setTag(holder);
            } else {
                holder = (ViewHolder) convertView.getTag();
            }

            GameItem item = getItem(position);
            holder.title.setText(item.name);
            holder.system.setText(item.systemTag);
            holder.filename.setText(item.fileName);

            // Dynamic platform badge accents
            if ("NEO GEO".equals(item.systemTag)) {
                holder.system.setBackgroundResource(R.drawable.bg_badge_neogeo);
                holder.system.setTextColor(0xFFE8A702);
            } else if ("CPS-1".equals(item.systemTag) || "CPS-2".equals(item.systemTag)) {
                holder.system.setBackgroundResource(R.drawable.bg_badge_cps);
                holder.system.setTextColor(0xFF38BDF8);
            } else if ("PGM".equals(item.systemTag)) {
                holder.system.setBackgroundResource(R.drawable.bg_badge_pgm);
                holder.system.setTextColor(0xFFC084FC);
            } else {
                holder.system.setBackgroundResource(R.drawable.bg_badge_arcade);
                holder.system.setTextColor(0xFF9CA3AF);
            }

            View.OnClickListener clickListener = v -> launchGameItem(item);
            convertView.setOnClickListener(clickListener);
            holder.playBtn.setOnClickListener(clickListener);

            return convertView;
        }
    }

    private static class ViewHolder {
        TextView title;
        TextView system;
        TextView filename;
        TextView playBtn;
    }

    private void applyFilter(String query) {
        currentSearchQuery = query == null ? "" : query.trim().toLowerCase();
        displayList.clear();
        if (currentSearchQuery.isEmpty()) {
            displayList.addAll(masterList);
        } else {
            for (GameItem item : masterList) {
                boolean matchTitle = item.name != null && item.name.toLowerCase().contains(currentSearchQuery);
                boolean matchFile = item.fileName != null && item.fileName.toLowerCase().contains(currentSearchQuery);
                boolean matchSystem = item.systemTag != null && item.systemTag.toLowerCase().contains(currentSearchQuery);
                if (matchTitle || matchFile || matchSystem) {
                    displayList.add(item);
                }
            }
        }
        gameAdapter.notifyDataSetChanged();
        if (masterList.isEmpty()) {
            tvRomStatus.setText("Ready to load game");
        } else if (!currentSearchQuery.isEmpty()) {
            tvRomStatus.setText("Showing " + displayList.size() + " of " + masterList.size() + " games");
        } else {
            tvRomStatus.setText("Found " + masterList.size() + " arcade game(s)");
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
        etSearch = findViewById(R.id.et_search);
        lvGames = findViewById(R.id.lv_games);

        gameAdapter = new GameAdapter();
        lvGames.setAdapter(gameAdapter);

        btnSelectRom.setOnClickListener(v -> openFilePicker());
        btnSelectFolder.setOnClickListener(v -> openFolderPicker());

        etSearch.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                applyFilter(s == null ? "" : s.toString());
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        lvGames.setOnItemClickListener((parent, view, position, id) -> {
            if (position >= 0 && position < displayList.size()) {
                launchGameItem(displayList.get(position));
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
        scanCommonPhonePaths();
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
                File fbneoSystemDir = new File(systemDir, "fbneo");
                if (!fbneoSystemDir.exists()) fbneoSystemDir.mkdirs();
                File fbneoRomsDir = new File(romsDir, "fbneo");
                if (!fbneoRomsDir.exists()) fbneoRomsDir.mkdirs();

                getSavesDir(); // Ensure saves directory exists

                for (String bios : bioses) {
                    if (!bios.toLowerCase().endsWith(".zip")) continue;
                    File targetRoms = new File(romsDir, bios);
                    File targetBios = new File(biosDir, bios);
                    File targetSystem = new File(systemDir, bios);
                    File targetFbneoSys = new File(fbneoSystemDir, bios);
                    File targetFbneoRom = new File(fbneoRomsDir, bios);

                    if (!targetRoms.exists() || targetRoms.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetRoms);
                    }
                    if (!targetBios.exists() || targetBios.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetBios);
                    }
                    if (!targetSystem.exists() || targetSystem.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetSystem);
                    }
                    if (!targetFbneoSys.exists() || targetFbneoSys.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetFbneoSys);
                    }
                    if (!targetFbneoRom.exists() || targetFbneoRom.length() == 0) {
                        copyAssetToFile("bios/" + bios, targetFbneoRom);
                    }
                }

                ensureBiosSynchronized();

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
        if (!src.exists() || src.length() < 1000) {
            src = new File(getRomsDir(), "neogeo.zip");
        }
        if (!src.exists() || src.length() < 1000) {
            src = new File(getSystemDir(), "neogeo.zip");
        }

        if (src.exists() && src.length() > 1000) {
            File[] targets = {
                new File(getRomsDir(), "neogeo.zip"),
                new File(getBiosDir(), "neogeo.zip"),
                new File(getSystemDir(), "neogeo.zip"),
                new File(new File(getSystemDir(), "fbneo"), "neogeo.zip"),
                new File(new File(getRomsDir(), "fbneo"), "neogeo.zip")
            };
            for (File t : targets) {
                if (!t.getParentFile().exists()) {
                    t.getParentFile().mkdirs();
                }
                if (!t.exists() || t.length() != src.length()) {
                    try {
                        java.nio.file.Files.copy(src.toPath(), t.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                    } catch (Exception ignored) {}
                }
            }
        }
    }

    private boolean isBiosPresent() {
        File[] checks = {
            new File(getRomsDir(), "neogeo.zip"),
            new File(getBiosDir(), "neogeo.zip"),
            new File(getSystemDir(), "neogeo.zip"),
            new File(new File(getSystemDir(), "fbneo"), "neogeo.zip")
        };
        for (File f : checks) {
            if (f.exists() && f.length() > 1000) return true;
        }
        return false;
    }

    private static String detectSystem(String fileName) {
        if (fileName == null) return "ARCADE";
        String base = fileName.toLowerCase();
        if (base.endsWith(".zip")) {
            base = base.substring(0, base.length() - 4);
        }

        String[] neoGeoPrefixes = {
            "mslug", "kof", "garou", "lastbld", "samsho", "rbff", "sengoku",
            "wjammers", "spinmast", "nam1975", "fatfury", "fatfur", "aof", "neogal",
            "shocktro", "shocktr", "blazstar", "pulstar"
        };
        for (String prefix : neoGeoPrefixes) {
            if (base.startsWith(prefix)) return "NEO GEO";
        }

        String[] cps2Prefixes = {
            "sfa", "vsav", "mvsc", "xmcota", "msh", "mshvsf", "xmvsf",
            "dstlk", "nwarr", "progear", "1944", "ddsom", "ddtod", "ssf2",
            "vhunt2", "armwar", "cybots", "avsp", "jojoba"
        };
        for (String prefix : cps2Prefixes) {
            if (base.startsWith(prefix)) return "CPS-2";
        }

        String[] cps1Prefixes = {
            "sf2", "dino", "punisher", "cworld2", "ffight", "ghouls", "strider",
            "mercs", "captcomm", "wof"
        };
        for (String prefix : cps1Prefixes) {
            if (base.startsWith(prefix)) return "CPS-1";
        }

        String[] pgmPrefixes = {
            "martmast", "kov", "orlegend", "svg", "drgw2", "demonwld"
        };
        for (String prefix : pgmPrefixes) {
            if (base.startsWith(prefix)) return "PGM";
        }

        return "ARCADE";
    }

    private boolean isNeoGeoRom(String fileName) {
        return "NEO GEO".equals(detectSystem(fileName));
    }

    private void updateBiosStatus() {
        ensureBiosSynchronized();
        if (isBiosPresent()) {
            tvBiosStatus.setText("✓ Neo Geo BIOS: Ready (neogeo.zip active)");
            tvBiosStatus.setTextColor(0xFF34D399); // status_green
        } else {
            tvBiosStatus.setText("⚠ Neo Geo BIOS: Not found (Tap to install neogeo.zip)");
            tvBiosStatus.setTextColor(0xFFE8A702); // accent_gold
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
        String rawName = "game.zip";
        if (doc != null && doc.getName() != null) {
            rawName = doc.getName();
        } else {
            String path = uri.getPath();
            if (path != null) {
                int idx = path.lastIndexOf('/');
                if (idx != -1) rawName = path.substring(idx + 1);
            }
        }
        // SEC-FIND-02: Sanitize filename to prevent directory traversal and arbitrary file overwrite
        String cleanName = new File(rawName).getName().replace('/', '_').replace('\\', '_');
        if (cleanName.equals("..") || cleanName.equals(".") || cleanName.contains("..") || cleanName.trim().isEmpty()) {
            cleanName = "safe_" + System.currentTimeMillis() + ".zip";
        }
        return cleanName;
    }

    private void handleSelectedFileUri(Uri uri) {
        String fileName = getFileNameFromUri(uri);
        Toast.makeText(this, "Preparing " + fileName + "...", Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            try {
                File target = new File(getRomsDir(), fileName);
                if (!target.getCanonicalPath().startsWith(getRomsDir().getCanonicalPath())) {
                    throw new SecurityException("Path traversal attempt in URI filename: " + fileName);
                }
                copyUriToFile(uri, target);

                // If user selected neogeo.zip, update BIOS status & synchronize
                if (fileName.equalsIgnoreCase("neogeo.zip")) {
                    ensureBiosSynchronized();
                    runOnUiThread(() -> {
                        updateBiosStatus();
                        Toast.makeText(MainActivity.this, "Neo Geo BIOS installed successfully!", Toast.LENGTH_SHORT).show();
                    });
                    return;
                }

                File finalRom = RomNormalizer.getNormalizedRomFile(MainActivity.this, target);

                runOnUiThread(() -> {
                    updateBiosStatus();
                    launchDirect(finalRom.getAbsolutePath());
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
                masterList.clear();

                for (DocumentFile f : zipFiles) {
                    String name = f.getName();
                    String friendly = formatFriendlyName(name);
                    masterList.add(new GameItem(friendly, name, f.getUri(), null, detectSystem(name)));
                }

                applyFilter(etSearch != null && etSearch.getText() != null ? etSearch.getText().toString() : "");
                updateBiosStatus();
            });
        }).start();
    }

    private void scanInternalRoms() {
        File[] files = getRomsDir().listFiles((dir, name) -> name.toLowerCase().endsWith(".zip"));
        if (files == null) return;

        boolean added = false;
        for (File f : files) {
            String name = f.getName();
            if (name.equalsIgnoreCase("neogeo.zip")) continue;
            boolean exists = false;
            for (GameItem item : masterList) {
                if (item.fileName.equalsIgnoreCase(name)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                String friendly = formatFriendlyName(name);
                masterList.add(new GameItem(friendly, name, null, f.getAbsolutePath(), detectSystem(name)));
                added = true;
            }
        }
        if (added) {
            applyFilter(etSearch != null && etSearch.getText() != null ? etSearch.getText().toString() : "");
        }
    }

    private void scanCommonPhonePaths() {
        String[] candidateDirs = {
            "/sdcard/Download/Roms",
            "/storage/emulated/0/Download/Roms",
            "/sdcard/Download/roms",
            "/storage/emulated/0/Download/roms",
            "/sdcard/roms",
            "/storage/emulated/0/roms",
            "/sdcard/Roms",
            "/storage/emulated/0/Roms"
        };
        for (String dirPath : candidateDirs) {
            File dir = new File(dirPath);
            if (dir.exists() && dir.isDirectory() && dir.canRead()) {
                File[] zips = dir.listFiles((d, name) -> name.toLowerCase().endsWith(".zip"));
                if (zips != null && zips.length > 0) {
                    boolean added = false;
                    for (File z : zips) {
                        String name = z.getName();
                        if (name.equalsIgnoreCase("neogeo.zip")) {
                            File targetBios = new File(getBiosDir(), "neogeo.zip");
                            if (!targetBios.exists() || targetBios.length() < 1000) {
                                try {
                                    java.nio.file.Files.copy(z.toPath(), targetBios.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                                } catch (Exception ignored) {}
                            }
                            continue;
                        }
                        boolean exists = false;
                        for (GameItem item : masterList) {
                            if (item.fileName.equalsIgnoreCase(name)) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            String friendly = formatFriendlyName(name);
                            masterList.add(new GameItem(friendly, name, null, z.getAbsolutePath(), detectSystem(name)));
                            added = true;
                        }
                    }
                    if (added) {
                        applyFilter(etSearch != null && etSearch.getText() != null ? etSearch.getText().toString() : "");
                    }
                    updateBiosStatus();
                    break;
                }
            }
        }
    }

    private static final Map<String, String> GAME_DICTIONARY = new HashMap<>();
    static {
        // Neo Geo
        GAME_DICTIONARY.put("mslug", "Metal Slug - Super Vehicle-001");
        GAME_DICTIONARY.put("mslug2", "Metal Slug 2");
        GAME_DICTIONARY.put("mslugx", "Metal Slug X");
        GAME_DICTIONARY.put("mslug3", "Metal Slug 3");
        GAME_DICTIONARY.put("mslug4", "Metal Slug 4");
        GAME_DICTIONARY.put("mslug5", "Metal Slug 5");
        GAME_DICTIONARY.put("kof94", "The King of Fighters '94");
        GAME_DICTIONARY.put("kof95", "The King of Fighters '95");
        GAME_DICTIONARY.put("kof96", "The King of Fighters '96");
        GAME_DICTIONARY.put("kof97", "The King of Fighters '97");
        GAME_DICTIONARY.put("kof98", "The King of Fighters '98");
        GAME_DICTIONARY.put("kof99", "The King of Fighters '99");
        GAME_DICTIONARY.put("kof2000", "The King of Fighters 2000");
        GAME_DICTIONARY.put("kof2001", "The King of Fighters 2001");
        GAME_DICTIONARY.put("kof2002", "The King of Fighters 2002");
        GAME_DICTIONARY.put("kof2003", "The King of Fighters 2003");
        GAME_DICTIONARY.put("garou", "Garou: Mark of the Wolves");
        GAME_DICTIONARY.put("lastbld", "The Last Blade");
        GAME_DICTIONARY.put("lastbld2", "The Last Blade 2");
        GAME_DICTIONARY.put("samsho", "Samurai Shodown");
        GAME_DICTIONARY.put("samsho2", "Samurai Shodown II");
        GAME_DICTIONARY.put("samsho3", "Samurai Shodown III");
        GAME_DICTIONARY.put("samsho4", "Samurai Shodown IV");
        GAME_DICTIONARY.put("samsho5", "Samurai Shodown V");
        GAME_DICTIONARY.put("rbff", "Real Bout Fatal Fury");
        GAME_DICTIONARY.put("rbffspec", "Real Bout Fatal Fury Special");
        GAME_DICTIONARY.put("rbff2", "Real Bout Fatal Fury 2");
        GAME_DICTIONARY.put("fatfury1", "Fatal Fury");
        GAME_DICTIONARY.put("fatfury2", "Fatal Fury 2");
        GAME_DICTIONARY.put("fatfury3", "Fatal Fury 3");
        GAME_DICTIONARY.put("fatfursp", "Fatal Fury Special");
        GAME_DICTIONARY.put("aof", "Art of Fighting");
        GAME_DICTIONARY.put("aof2", "Art of Fighting 2");
        GAME_DICTIONARY.put("aof3", "Art of Fighting 3");
        GAME_DICTIONARY.put("wjammers", "Windjammers");
        GAME_DICTIONARY.put("spinmast", "Spinmaster");
        GAME_DICTIONARY.put("nam1975", "NAM-1975");
        GAME_DICTIONARY.put("sengoku", "Sengoku");
        GAME_DICTIONARY.put("sengoku2", "Sengoku 2");
        GAME_DICTIONARY.put("sengoku3", "Sengoku 3");
        GAME_DICTIONARY.put("shocktro", "Shock Troopers");
        GAME_DICTIONARY.put("shocktr2", "Shock Troopers: 2nd Squad");
        GAME_DICTIONARY.put("blazstar", "Blazing Star");
        GAME_DICTIONARY.put("pulstar", "Pulstar");

        // Capcom CPS-1 / CPS-2
        GAME_DICTIONARY.put("sf2", "Street Fighter II: The World Warrior");
        GAME_DICTIONARY.put("sf2ce", "Street Fighter II': Champion Edition");
        GAME_DICTIONARY.put("sf2hf", "Street Fighter II' Turbo: Hyper Fighting");
        GAME_DICTIONARY.put("ssf2", "Super Street Fighter II: The New Challengers");
        GAME_DICTIONARY.put("ssf2t", "Super Street Fighter II Turbo");
        GAME_DICTIONARY.put("sfa", "Street Fighter Alpha");
        GAME_DICTIONARY.put("sfa2", "Street Fighter Alpha 2");
        GAME_DICTIONARY.put("sfa3", "Street Fighter Alpha 3");
        GAME_DICTIONARY.put("dstlk", "Darkstalkers: The Night Warriors");
        GAME_DICTIONARY.put("nwarr", "Night Warriors: Darkstalkers' Revenge");
        GAME_DICTIONARY.put("vsav", "Vampire Savior: The Lord of Vampire");
        GAME_DICTIONARY.put("vsav2", "Vampire Savior 2: The Lord of Vampire");
        GAME_DICTIONARY.put("vhunt2", "Vampire Hunter 2: Darkstalkers' Revenge");
        GAME_DICTIONARY.put("xmcota", "X-Men: Children of the Atom");
        GAME_DICTIONARY.put("msh", "Marvel Super Heroes");
        GAME_DICTIONARY.put("xmvsf", "X-Men vs. Street Fighter");
        GAME_DICTIONARY.put("mshvsf", "Marvel Super Heroes vs. Street Fighter");
        GAME_DICTIONARY.put("mvsc", "Marvel vs. Capcom: Clash of Super Heroes");
        GAME_DICTIONARY.put("dino", "Cadillacs and Dinosaurs");
        GAME_DICTIONARY.put("punisher", "The Punisher");
        GAME_DICTIONARY.put("captcomm", "Captain Commando");
        GAME_DICTIONARY.put("ffight", "Final Fight");
        GAME_DICTIONARY.put("wof", "Warriors of Fate");
        GAME_DICTIONARY.put("armwar", "Armored Warriors");
        GAME_DICTIONARY.put("cybots", "Cyberbots: Fullmetal Madness");
        GAME_DICTIONARY.put("progear", "Progear");
        GAME_DICTIONARY.put("1944", "1944: The Loop Master");
        GAME_DICTIONARY.put("ddtod", "Dungeons & Dragons: Tower of Doom");
        GAME_DICTIONARY.put("ddsom", "Dungeons & Dragons: Shadow over Mystara");
        GAME_DICTIONARY.put("avsp", "Alien vs. Predator");
        GAME_DICTIONARY.put("jojoba", "JoJo's Bizarre Adventure");

        // IGS PGM
        GAME_DICTIONARY.put("martmast", "Martial Masters");
        GAME_DICTIONARY.put("kov", "Knights of Valour");
        GAME_DICTIONARY.put("kovplus", "Knights of Valour Plus");
        GAME_DICTIONARY.put("orlegend", "Oriental Legend");
        GAME_DICTIONARY.put("svg", "Spectral vs. Generation");
        GAME_DICTIONARY.put("drgw2", "Dragon World II");
        GAME_DICTIONARY.put("demonwld", "Demon Front");
    }

    private static String formatFriendlyName(String fileName) {
        if (fileName == null) return "";
        String base = fileName.toLowerCase();
        if (base.endsWith(".zip")) {
            base = base.substring(0, base.length() - 4);
        }
        if (GAME_DICTIONARY.containsKey(base)) {
            return GAME_DICTIONARY.get(base);
        }
        // Fallback: strip .zip, replace _ and - with spaces, convert to title case
        String cleaned = base.replace('_', ' ').replace('-', ' ').trim();
        return toTitleCase(cleaned);
    }

    private static String toTitleCase(String text) {
        if (text == null || text.isEmpty()) return "";
        String[] words = text.split("\\s+");
        StringBuilder sb = new StringBuilder();
        for (String w : words) {
            if (!w.isEmpty()) {
                if (sb.length() > 0) sb.append(' ');
                sb.append(Character.toUpperCase(w.charAt(0)));
                if (w.length() > 1) {
                    sb.append(w.substring(1).toLowerCase());
                }
            }
        }
        return sb.toString();
    }

    private void launchGameItem(GameItem item) {
        Toast.makeText(this, "Preparing " + item.fileName + "...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                File romToLaunch;
                if (item.localPath != null && new File(item.localPath).exists()) {
                    romToLaunch = RomNormalizer.getNormalizedRomFile(MainActivity.this, new File(item.localPath));
                } else if (item.uri != null) {
                    File target = new File(getRomsDir(), item.fileName);
                    if (!target.exists() || target.length() == 0) {
                        copyUriToFile(item.uri, target);
                    }
                    romToLaunch = RomNormalizer.getNormalizedRomFile(MainActivity.this, target);
                } else {
                    return;
                }
                final String finalPath = romToLaunch.getAbsolutePath();
                runOnUiThread(() -> launchDirect(finalPath));
            } catch (Exception e) {
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Error: " + e.getMessage(), Toast.LENGTH_LONG).show());
            }
        }).start();
    }


    private void launchDirect(String romPath) {
        ensureBiosSynchronized();

        File romFile = new File(romPath);
        String fileName = romFile.getName();
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

        // Also ensure neogeo.zip is placed in finalRomPath's parent directory if writable
        File romParent = romFile.getParentFile();
        if (romParent != null && romParent.canWrite()) {
            File biosInRomDir = new File(romParent, "neogeo.zip");
            File masterBios = new File(getBiosDir(), "neogeo.zip");
            if (masterBios.exists() && (!biosInRomDir.exists() || biosInRomDir.length() < 1000)) {
                try {
                    java.nio.file.Files.copy(masterBios.toPath(), biosInRomDir.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                } catch (Exception ignored) {}
            }
        }

        Intent intent = new Intent(this, ArcadeActivity.class);
        intent.putExtra("ROM_PATH", romPath);
        intent.putExtra("BIOS_DIR", getBiosDir().getAbsolutePath());
        intent.putExtra("SYSTEM_DIR", getSystemDir().getAbsolutePath());
        intent.putExtra("ROMS_DIR", getRomsDir().getAbsolutePath());
        intent.putExtra("SAVE_DIR", getSavesDir().getAbsolutePath());
        startActivity(intent);
    }
}
