package com.retro.handheld;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.documentfile.provider.DocumentFile;

import com.arcade.engine.GameItem;
import com.arcade.engine.NetworkRomSource;
import com.arcade.engine.OfflineDownloadManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CancellationException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Modern ROM browser for Game Boy, Game Boy Color, and Game Boy Advance titles.
 * Discovers ROMs from local phone storage, SAF document trees, and network ROM sources.
 */
public class MainActivity extends Activity {

    private static final int REQ_SELECT_ROM = 1001;
    private static final int REQ_SELECT_FOLDER = 1002;
    private static final int REQ_STORAGE_PERMISSION = 2001;

    private static final String PREF_NAME = "handheld_player_prefs";
    private static final String KEY_ACTIVE_SERVER = "network_server_url";
    private static final String KEY_RECENT_SERVERS = "recent_server_urls";

    private TextView tvCoreStatus;
    private TextView tvRomStatus;
    private Button btnSelectRom;
    private Button btnSelectFolder;
    private Button btnNetworkSource;
    private View cardNetworkSource;
    private TextView tvNetworkStatus;
    private Button btnNetworkSaveAll;
    private Button btnNetworkSync;
    private Button btnNetworkDisconnect;
    private Button btnFilterAll;
    private Button btnFilterOffline;
    private Button btnFilterNetwork;
    private OfflineDownloadManager.FilterTab currentFilterTab = OfflineDownloadManager.FilterTab.ALL;
    private EditText etSearch;
    private ListView lvGames;

    private AlertDialog activeProgressDialog;
    private OfflineDownloadManager.BatchQueue activeBatchQueue;
    private AtomicBoolean activeSingleCancelSignal;

    private final List<GameItem> masterList = new ArrayList<>();
    private final List<GameItem> displayList = new ArrayList<>();
    private GameAdapter gameAdapter;
    private String currentSearchQuery = "";

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
                holder.netBadge = convertView.findViewById(R.id.tv_game_net_badge);
                holder.filename = convertView.findViewById(R.id.tv_game_filename);
                holder.downloadBtn = convertView.findViewById(R.id.tv_download_btn);
                holder.playBtn = convertView.findViewById(R.id.tv_play_btn);
                convertView.setTag(holder);
            } else {
                holder = (ViewHolder) convertView.getTag();
            }

            GameItem item = getItem(position);
            holder.title.setText(item.name != null ? item.name : item.fileName);
            holder.system.setText(item.systemTag != null ? item.systemTag : "GAME BOY");
            holder.filename.setText(item.fileName);

            // Dynamic platform badge accents
            if ("GBA".equalsIgnoreCase(item.systemTag)) {
                holder.system.setBackgroundResource(R.drawable.bg_badge_gba);
                holder.system.setTextColor(0xFF818CF8);
            } else if ("GBC".equalsIgnoreCase(item.systemTag)) {
                holder.system.setBackgroundResource(R.drawable.bg_badge_gbc);
                holder.system.setTextColor(0xFFC084FC);
            } else {
                holder.system.setBackgroundResource(R.drawable.bg_badge_gb);
                holder.system.setTextColor(0xFF9BBC0F);
            }

            // Network badge
            if (holder.netBadge != null) {
                if (item.isNetwork) {
                    holder.netBadge.setVisibility(View.VISIBLE);
                    holder.netBadge.setText("🌐 LAN");
                } else {
                    holder.netBadge.setVisibility(View.GONE);
                }
            }

            // Dual-action button states: Download / Offline & Play
            boolean downloaded = item.isDownloadedQuick();
            if (downloaded) {
                holder.playBtn.setText("PLAY ▶");
                holder.playBtn.setBackgroundResource(R.drawable.bg_play_cue);
                holder.playBtn.setTextColor(0xFFF1F2F6);

                holder.downloadBtn.setText("✓ SAVED");
                holder.downloadBtn.setBackgroundResource(R.drawable.bg_saved_cue);
                holder.downloadBtn.setTextColor(0xFF34D399);
            } else {
                holder.playBtn.setText("PLAY ▶");
                holder.playBtn.setBackgroundResource(R.drawable.bg_play_cue);
                holder.playBtn.setTextColor(0xFFF1F2F6);

                holder.downloadBtn.setText("⬇ SAVE");
                holder.downloadBtn.setBackgroundResource(R.drawable.bg_download_cue);
                holder.downloadBtn.setTextColor(0xFF10B981);
            }

            holder.downloadBtn.setOnClickListener(v -> {
                if (!item.isDownloadedQuick()) {
                    startRomDownload(item, false);
                } else {
                    Toast.makeText(MainActivity.this, (item.name != null ? item.name : item.fileName) + " is already saved offline", Toast.LENGTH_SHORT).show();
                }
            });

            View.OnClickListener playClickListener = v -> {
                if (!item.isDownloadedQuick()) {
                    startRomDownload(item, true);
                } else {
                    launchGameItem(item);
                }
            };
            convertView.setOnClickListener(playClickListener);
            holder.playBtn.setOnClickListener(playClickListener);

            convertView.setOnLongClickListener(v -> {
                showGameContextMenu(item);
                return true;
            });

            return convertView;
        }
    }

    private static class ViewHolder {
        TextView title;
        TextView system;
        TextView netBadge;
        TextView filename;
        TextView downloadBtn;
        TextView playBtn;
    }

    private void setFilterTab(OfflineDownloadManager.FilterTab tab) {
        currentFilterTab = tab;
        updateFilterTabUI();
        applyFilter(currentSearchQuery);
    }

    private void updateFilterTabUI() {
        if (btnFilterAll == null || btnFilterOffline == null || btnFilterNetwork == null) return;

        btnFilterAll.setBackgroundResource(currentFilterTab == OfflineDownloadManager.FilterTab.ALL ?
                R.drawable.bg_tab_selected : R.drawable.bg_tab_unselected);
        btnFilterAll.setTextColor(currentFilterTab == OfflineDownloadManager.FilterTab.ALL ?
                0xFFF1F2F6 : 0xFF8E92A4);

        btnFilterOffline.setBackgroundResource(currentFilterTab == OfflineDownloadManager.FilterTab.OFFLINE ?
                R.drawable.bg_tab_selected : R.drawable.bg_tab_unselected);
        btnFilterOffline.setTextColor(currentFilterTab == OfflineDownloadManager.FilterTab.OFFLINE ?
                0xFFF1F2F6 : 0xFF8E92A4);

        btnFilterNetwork.setBackgroundResource(currentFilterTab == OfflineDownloadManager.FilterTab.NETWORK ?
                R.drawable.bg_tab_selected : R.drawable.bg_tab_unselected);
        btnFilterNetwork.setTextColor(currentFilterTab == OfflineDownloadManager.FilterTab.NETWORK ?
                0xFFF1F2F6 : 0xFF8E92A4);
    }

    private void updateFilterCounts() {
        if (btnFilterAll == null || btnFilterOffline == null || btnFilterNetwork == null) return;
        int allCount = OfflineDownloadManager.countGames(masterList, OfflineDownloadManager.FilterTab.ALL, getRomsDir());
        int offlineCount = OfflineDownloadManager.countGames(masterList, OfflineDownloadManager.FilterTab.OFFLINE, getRomsDir());
        int networkCount = OfflineDownloadManager.countGames(masterList, OfflineDownloadManager.FilterTab.NETWORK, getRomsDir());

        btnFilterAll.setText("ALL (" + allCount + ")");
        btnFilterOffline.setText("OFFLINE (" + offlineCount + ")");
        btnFilterNetwork.setText("NETWORK (" + networkCount + ")");
    }

    private void showGameContextMenu(GameItem item) {
        boolean downloaded = item.isDownloaded(getRomsDir());
        List<String> options = new ArrayList<>();
        options.add("Play Game");
        if (!downloaded) {
            options.add("Download for Offline Play");
        } else {
            options.add("Delete Local Copy");
        }

        String[] optionsArray = options.toArray(new String[0]);
        new AlertDialog.Builder(this)
                .setTitle(item.name != null ? item.name : item.fileName)
                .setItems(optionsArray, (dialog, which) -> {
                    String selected = optionsArray[which];
                    if ("Play Game".equals(selected)) {
                        if (!item.isDownloaded(getRomsDir())) {
                            startRomDownload(item, true);
                        } else {
                            launchGameItem(item);
                        }
                    } else if ("Download for Offline Play".equals(selected)) {
                        startRomDownload(item, false);
                    } else if ("Delete Local Copy".equals(selected)) {
                        long reclaimed = OfflineDownloadManager.deleteLocalCopy(item, getRomsDir());
                        if (!OfflineDownloadManager.shouldRetainAfterDeletion(item) && item.uri == null) {
                            masterList.remove(item);
                        }
                        applyFilter(currentSearchQuery);
                        updateFilterCounts();
                        String spaceStr = OfflineDownloadManager.formatBytes(reclaimed);
                        Toast.makeText(MainActivity.this, "Deleted local copy (" + spaceStr + " reclaimed)", Toast.LENGTH_SHORT).show();
                    }
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void applyFilter(String query) {
        currentSearchQuery = query == null ? "" : query.trim().toLowerCase(Locale.ROOT);
        displayList.clear();
        displayList.addAll(OfflineDownloadManager.filterGames(masterList, currentFilterTab, currentSearchQuery, getRomsDir()));
        gameAdapter.notifyDataSetChanged();
        updateFilterCounts();
        updateFilterTabUI();

        if (masterList.isEmpty()) {
            tvRomStatus.setText("Ready to load game");
        } else if (!currentSearchQuery.isEmpty()) {
            tvRomStatus.setText("Showing " + displayList.size() + " of " + masterList.size() + " games");
        } else {
            String filterLabel = currentFilterTab == OfflineDownloadManager.FilterTab.OFFLINE ? "offline " :
                    currentFilterTab == OfflineDownloadManager.FilterTab.NETWORK ? "network " : "";
            tvRomStatus.setText("Found " + displayList.size() + " " + filterLabel + "handheld game(s)");
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvCoreStatus = findViewById(R.id.tv_core_status);
        tvRomStatus = findViewById(R.id.tv_rom_status);
        btnSelectRom = findViewById(R.id.btn_select_rom);
        btnSelectFolder = findViewById(R.id.btn_select_folder);
        btnNetworkSource = findViewById(R.id.btn_network_source);
        cardNetworkSource = findViewById(R.id.card_network_source);
        tvNetworkStatus = findViewById(R.id.tv_network_status);
        btnNetworkSaveAll = findViewById(R.id.btn_network_save_all);
        btnNetworkSync = findViewById(R.id.btn_network_sync);
        btnNetworkDisconnect = findViewById(R.id.btn_network_disconnect);
        btnFilterAll = findViewById(R.id.btn_filter_all);
        btnFilterOffline = findViewById(R.id.btn_filter_offline);
        btnFilterNetwork = findViewById(R.id.btn_filter_network);
        etSearch = findViewById(R.id.et_search);
        lvGames = findViewById(R.id.lv_games);

        gameAdapter = new GameAdapter();
        lvGames.setAdapter(gameAdapter);

        // Ensure directories exist
        getRomsDir();
        getBiosDir();
        getSystemDir();
        getSavesDir();

        // Restore active network server if previously saved
        String activeServer = getActiveServer();
        if (activeServer != null && !activeServer.trim().isEmpty()) {
            cardNetworkSource.setVisibility(View.VISIBLE);
            tvNetworkStatus.setText("🌐 " + activeServer);
            scanNetworkSource(activeServer, false);
        }

        btnSelectRom.setOnClickListener(v -> openFilePicker());
        btnSelectFolder.setOnClickListener(v -> openFolderPicker());
        btnNetworkSource.setOnClickListener(v -> showNetworkSourceDialog());

        btnNetworkSaveAll.setOnClickListener(v -> startBatchDownload());
        btnNetworkSync.setOnClickListener(v -> {
            String s = getActiveServer();
            if (!s.isEmpty()) scanNetworkSource(s, false);
        });
        btnNetworkDisconnect.setOnClickListener(v -> disconnectNetworkSource());

        btnFilterAll.setOnClickListener(v -> setFilterTab(OfflineDownloadManager.FilterTab.ALL));
        btnFilterOffline.setOnClickListener(v -> setFilterTab(OfflineDownloadManager.FilterTab.OFFLINE));
        btnFilterNetwork.setOnClickListener(v -> setFilterTab(OfflineDownloadManager.FilterTab.NETWORK));

        etSearch.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                applyFilter(s != null ? s.toString() : "");
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        checkStoragePermission();

        scanInternalRoms();
        scanCommonPhonePaths();
    }

    @Override
    protected void onResume() {
        super.onResume();
        scanInternalRoms();
        scanCommonPhonePaths();
        applyFilter(currentSearchQuery);
    }

    private File getRomsDir() {
        File dir = new File(getFilesDir(), "roms/handheld");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private File getBiosDir() {
        File dir = new File(getFilesDir(), "bios");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private File getSystemDir() {
        File dir = new File(getFilesDir(), "system");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private File getSavesDir() {
        File dir = new File(getFilesDir(), "saves/handheld");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private void checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivity(intent);
                } catch (Exception e) {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(intent);
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, REQ_STORAGE_PERMISSION);
            }
        }
    }

    private void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{
                "application/zip",
                "application/octet-stream",
                "*/*"
        });
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
                try {
                    getContentResolver().takePersistableUriPermission(
                            uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION
                    );
                } catch (Exception ignored) {}

                String fileName = getFileNameFromUri(uri);
                if (fileName != null && isSupportedHandheldRom(fileName)) {
                    GameItem item = new GameItem(
                            formatFriendlyName(fileName),
                            fileName,
                            uri,
                            null,
                            detectSystem(fileName)
                    );
                    masterList.add(0, item);
                    applyFilter(currentSearchQuery);
                    launchGameItem(item);
                }
            }
        } else if (requestCode == REQ_SELECT_FOLDER) {
            Uri treeUri = data.getData();
            if (treeUri != null) {
                try {
                    getContentResolver().takePersistableUriPermission(
                            treeUri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION
                    );
                } catch (Exception ignored) {}
                scanDocumentTree(treeUri);
            }
        }
    }

    public static boolean isSupportedHandheldRom(String fileName) {
        if (fileName == null) return false;
        String lower = fileName.toLowerCase(Locale.ROOT);
        return lower.endsWith(".gb") ||
               lower.endsWith(".gbc") ||
               lower.endsWith(".gba") ||
               lower.endsWith(".zip");
    }

    public static String detectSystem(String fileName) {
        if (fileName == null) return "GAME BOY";
        String lower = fileName.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".gbc") || lower.contains(".gbc.") || lower.contains("(gbc)") || lower.contains("[gbc]")) {
            return "GBC";
        }
        if (lower.endsWith(".gba") || lower.contains(".gba.") || lower.contains("(gba)") || lower.contains("[gba]")) {
            return "GBA";
        }
        if (lower.endsWith(".gb") || lower.contains(".gb.") || lower.contains("(gb)") || lower.contains("[gb]")) {
            return "GAME BOY";
        }
        if (lower.contains("gbc") || lower.contains("color")) {
            return "GBC";
        }
        if (lower.contains("gba") || lower.contains("advance")) {
            return "GBA";
        }
        return "GAME BOY";
    }

    public static String formatFriendlyName(String fileName) {
        if (fileName == null || fileName.isEmpty()) return "Handheld Game";
        String name = fileName;
        for (String ext : new String[]{".gbc.zip", ".gba.zip", ".gb.zip", ".zip", ".gbc", ".gba", ".gb"}) {
            if (name.toLowerCase(Locale.ROOT).endsWith(ext)) {
                name = name.substring(0, name.length() - ext.length());
                break;
            }
        }
        name = name.replace('_', ' ').trim();
        return name.isEmpty() ? fileName : name;
    }

    private String getFileNameFromUri(Uri uri) {
        DocumentFile doc = DocumentFile.fromSingleUri(this, uri);
        if (doc != null && doc.getName() != null) {
            return new File(doc.getName()).getName();
        }
        String lastSegment = uri.getLastPathSegment();
        return lastSegment != null ? new File(lastSegment).getName() : "unknown_rom";
    }

    private void scanDocumentTree(Uri treeUri) {
        DocumentFile root = DocumentFile.fromTreeUri(this, treeUri);
        if (root == null || !root.isDirectory()) return;

        Toast.makeText(this, "Scanning folder...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            DocumentFile[] files = root.listFiles();
            boolean added = false;
            for (DocumentFile f : files) {
                if (!f.isDirectory()) {
                    String name = f.getName();
                    if (name != null && isSupportedHandheldRom(name)) {
                        boolean exists = false;
                        for (GameItem item : masterList) {
                            if (item.fileName.equalsIgnoreCase(name)) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            String friendly = formatFriendlyName(name);
                            masterList.add(new GameItem(friendly, name, f.getUri(), null, detectSystem(name)));
                            added = true;
                        }
                    }
                }
            }
            if (added) {
                runOnUiThread(() -> applyFilter(currentSearchQuery));
            }
        }).start();
    }

    private void scanInternalRoms() {
        File romsDir = getRomsDir();
        File[] files = romsDir.listFiles((dir, name) -> isSupportedHandheldRom(name));
        if (files == null) return;

        boolean added = false;
        for (File f : files) {
            String name = f.getName();
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
            applyFilter(currentSearchQuery);
        }
    }

    private void scanCommonPhonePaths() {
        String[] candidateDirs = {
            "/sdcard/Download/Roms",
            "/storage/emulated/0/Download/Roms",
            "/sdcard/Download/roms",
            "/storage/emulated/0/Download/roms",
            "/sdcard/Roms",
            "/storage/emulated/0/Roms",
            "/sdcard/roms",
            "/storage/emulated/0/roms"
        };
        for (String dirPath : candidateDirs) {
            File dir = new File(dirPath);
            if (dir.exists() && dir.isDirectory() && dir.canRead()) {
                File[] files = dir.listFiles((d, name) -> isSupportedHandheldRom(name));
                if (files != null && files.length > 0) {
                    boolean added = false;
                    for (File f : files) {
                        String name = f.getName();
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
                        applyFilter(currentSearchQuery);
                    }
                }
            }
        }
    }

    private void showNetworkSourceDialog() {
        View dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_network_source, null);
        EditText etUrl = dialogView.findViewById(R.id.et_server_url);
        TextView tvRecentLabel = dialogView.findViewById(R.id.tv_recent_label);
        ViewGroup layoutRecent = dialogView.findViewById(R.id.layout_recent_servers);
        Button btnDisconnect = dialogView.findViewById(R.id.btn_dialog_disconnect);
        Button btnCancel = dialogView.findViewById(R.id.btn_dialog_cancel);
        Button btnDirect = dialogView.findViewById(R.id.btn_dialog_direct);
        Button btnScan = dialogView.findViewById(R.id.btn_dialog_scan);

        String active = getActiveServer();
        if (!active.isEmpty()) {
            etUrl.setText(active);
            btnDisconnect.setVisibility(View.VISIBLE);
        }

        List<String> recents = getRecentServers();
        if (!recents.isEmpty()) {
            tvRecentLabel.setVisibility(View.VISIBLE);
            layoutRecent.setVisibility(View.VISIBLE);
            layoutRecent.removeAllViews();
            for (String url : recents) {
                TextView item = new TextView(this);
                item.setText("• " + url);
                item.setTextColor(0xFF818CF8);
                item.setTextSize(12);
                item.setPadding(0, 8, 0, 8);
                item.setOnClickListener(v -> etUrl.setText(url));
                layoutRecent.addView(item);
            }
        }

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setView(dialogView)
                .create();

        btnCancel.setOnClickListener(v -> dialog.dismiss());
        btnDisconnect.setOnClickListener(v -> {
            dialog.dismiss();
            disconnectNetworkSource();
        });
        btnScan.setOnClickListener(v -> {
            String raw = etUrl.getText().toString().trim();
            if (raw.isEmpty()) {
                Toast.makeText(MainActivity.this, "Please enter a server URL or IP address", Toast.LENGTH_SHORT).show();
                return;
            }
            dialog.dismiss();
            scanNetworkSource(raw, false);
        });
        btnDirect.setOnClickListener(v -> {
            String raw = etUrl.getText().toString().trim();
            if (raw.isEmpty()) {
                Toast.makeText(MainActivity.this, "Please enter a direct ROM URL", Toast.LENGTH_SHORT).show();
                return;
            }
            dialog.dismiss();
            scanNetworkSource(raw, true);
        });

        dialog.show();
    }

    private void scanNetworkSource(String rawUrl, boolean directZipMode) {
        Toast.makeText(this, "Connecting to network source...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                String normalized = NetworkRomSource.normalizeUrl(rawUrl);
                AtomicBoolean cancelSignal = new AtomicBoolean(false);
                List<NetworkRomSource.NetworkRomItem> netItems =
                        NetworkRomSource.fetchCatalogSync(normalized, cancelSignal);

                runOnUiThread(() -> {
                    if (netItems == null || netItems.isEmpty()) {
                        Toast.makeText(MainActivity.this, "No ROM files found on server", Toast.LENGTH_LONG).show();
                        return;
                    }

                    setActiveServer(normalized);
                    addRecentServer(normalized);
                    cardNetworkSource.setVisibility(View.VISIBLE);
                    tvNetworkStatus.setText("🌐 " + normalized);

                    for (NetworkRomSource.NetworkRomItem netItem : netItems) {
                        String fileName = netItem.getFileName();
                        if (!isSupportedHandheldRom(fileName)) continue;

                        GameItem existing = null;
                        for (GameItem item : masterList) {
                            if (item.fileName.equalsIgnoreCase(fileName)) {
                                existing = item;
                                break;
                            }
                        }

                        if (existing != null) {
                            existing.isNetwork = true;
                            existing.remoteUrl = netItem.getUrl();
                            if (existing.sizeBytes <= 0 && netItem.getSize() > 0) {
                                existing.sizeBytes = netItem.getSize();
                            }
                        } else {
                            String friendly = formatFriendlyName(fileName);
                            File localRom = new File(getRomsDir(), fileName);
                            String localPath = localRom.exists() && localRom.length() > 0 ? localRom.getAbsolutePath() : null;
                            GameItem newItem = new GameItem(
                                    friendly,
                                    fileName,
                                    null,
                                    localPath,
                                    detectSystem(fileName),
                                    netItem.getUrl(),
                                    true,
                                    netItem.getSize()
                            );
                            masterList.add(newItem);
                        }
                    }

                    applyFilter(currentSearchQuery);

                    if (directZipMode || (netItems.size() == 1 && netItems.get(0).isDirectZip())) {
                        String zipName = netItems.get(0).getFileName();
                        for (GameItem gi : masterList) {
                            if (gi.fileName.equalsIgnoreCase(zipName)) {
                                if (!gi.isDownloaded(getRomsDir())) {
                                    startRomDownload(gi, true);
                                } else {
                                    launchGameItem(gi);
                                }
                                break;
                            }
                        }
                    } else {
                        Toast.makeText(MainActivity.this, "Found " + netItems.size() + " ROM(s) from " + normalized, Toast.LENGTH_SHORT).show();
                    }
                });
            } catch (Exception e) {
                runOnUiThread(() -> Toast.makeText(MainActivity.this, "Network error: " + e.getMessage(), Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    private void disconnectNetworkSource() {
        setActiveServer("");
        cardNetworkSource.setVisibility(View.GONE);
        masterList.removeIf(item -> item.isNetwork && !item.isDownloaded(getRomsDir()) && item.uri == null);
        for (GameItem item : masterList) {
            if (item.isNetwork) {
                item.isNetwork = false;
                item.remoteUrl = null;
            }
        }
        currentFilterTab = OfflineDownloadManager.FilterTab.OFFLINE;
        applyFilter(currentSearchQuery);
        updateFilterCounts();
        updateFilterTabUI();
        Toast.makeText(this, "Disconnected from network ROM source", Toast.LENGTH_SHORT).show();
    }

    private void startBatchDownload() {
        List<GameItem> toDownload = OfflineDownloadManager.getUndownloadedNetworkGames(masterList, getRomsDir());
        if (toDownload.isEmpty()) {
            Toast.makeText(this, "All network games are already saved offline!", Toast.LENGTH_SHORT).show();
            return;
        }

        long totalBytes = OfflineDownloadManager.calculateTotalDownloadBytes(toDownload);
        View dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_download_progress, null);
        TextView tvTitle = dialogView.findViewById(R.id.tv_download_title);
        TextView tvSubtitle = dialogView.findViewById(R.id.tv_download_subtitle);
        ProgressBar pbProgress = dialogView.findViewById(R.id.pb_download_progress);
        TextView tvPercent = dialogView.findViewById(R.id.tv_download_percent);
        TextView tvDetails = dialogView.findViewById(R.id.tv_download_details);
        Button btnCancel = dialogView.findViewById(R.id.btn_cancel_download);

        tvTitle.setText("DOWNLOADING ALL ROMS (0/" + toDownload.size() + ")");
        tvSubtitle.setText("Starting queue...");

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setView(dialogView)
                .setCancelable(false)
                .create();

        OfflineDownloadManager.BatchQueue queue = new OfflineDownloadManager.BatchQueue(toDownload);
        activeBatchQueue = queue;

        btnCancel.setOnClickListener(v -> {
            queue.cancel();
            safeDismissDialog(dialog);
            activeProgressDialog = null;
            activeBatchQueue = null;
            Toast.makeText(MainActivity.this, "Batch download cancelled", Toast.LENGTH_SHORT).show();
        });

        dialog.show();

        new Thread(() -> {
            queue.run(getRomsDir(), new OfflineDownloadManager.BatchDownloadListener() {
                @Override
                public void onQueueStart(int totalCount, long totalBytes) {
                    runOnUiThread(() -> tvTitle.setText("DOWNLOADING ALL ROMS (0/" + totalCount + ")"));
                }

                    @Override
                    public void onItemStart(int index, int totalCount, GameItem item) {
                        runOnUiThread(() -> {
                            tvTitle.setText("DOWNLOADING (" + (index + 1) + "/" + totalCount + ")");
                            tvSubtitle.setText(item.name != null ? item.name : item.fileName);
                            pbProgress.setProgress(0);
                            tvPercent.setText("0%");
                        });
                    }

                    @Override
                    public void onItemProgress(int index, int totalCount, GameItem item, long bytesRead, long itemTotal, double speed) {
                        runOnUiThread(() -> {
                            if (itemTotal > 0) {
                                int pct = (int) Math.min(100, (bytesRead * 100) / itemTotal);
                                pbProgress.setProgress(pct);
                                tvPercent.setText(pct + "%");
                                tvDetails.setText(String.format(Locale.ROOT, "%.1f MB / %.1f MB (%.1f MB/s)",
                                        bytesRead / (1024.0 * 1024.0),
                                        itemTotal / (1024.0 * 1024.0),
                                        speed / (1024.0 * 1024.0)));
                            }
                        });
                    }

                    @Override
                    public void onItemComplete(int index, int totalCount, GameItem item, File file) {
                        runOnUiThread(() -> {
                            item.localPath = file.getAbsolutePath();
                            gameAdapter.notifyDataSetChanged();
                            updateFilterCounts();
                        });
                    }

                    @Override
                    public void onItemError(int index, int totalCount, GameItem item, Exception error) {
                        runOnUiThread(() -> {
                            if (!(error instanceof CancellationException)) {
                                Toast.makeText(MainActivity.this, "Failed: " + item.fileName, Toast.LENGTH_SHORT).show();
                            }
                        });
                    }

                    @Override
                    public void onQueueFinished(int completedCount, int totalCount, boolean cancelled) {
                        runOnUiThread(() -> {
                            safeDismissDialog(dialog);
                            activeProgressDialog = null;
                            activeBatchQueue = null;
                            updateFilterCounts();
                            if (!cancelled) {
                                Toast.makeText(MainActivity.this, "✓ Downloaded " + completedCount + " of " + totalCount + " games", Toast.LENGTH_LONG).show();
                            }
                        });
                    }
                });
            }).start();
        }

    private void startRomDownload(GameItem item, boolean autoLaunch) {
        if (item.remoteUrl == null) return;

        View dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_download_progress, null);
        TextView tvTitle = dialogView.findViewById(R.id.tv_download_title);
        TextView tvSubtitle = dialogView.findViewById(R.id.tv_download_subtitle);
        ProgressBar pbProgress = dialogView.findViewById(R.id.pb_download_progress);
        TextView tvPercent = dialogView.findViewById(R.id.tv_download_percent);
        TextView tvDetails = dialogView.findViewById(R.id.tv_download_details);
        Button btnCancel = dialogView.findViewById(R.id.btn_cancel_download);

        tvTitle.setText("DOWNLOADING ROM");
        tvSubtitle.setText(item.name != null ? item.name : item.fileName);

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setView(dialogView)
                .setCancelable(false)
                .create();

        activeProgressDialog = dialog;
        AtomicBoolean cancelSignal = new AtomicBoolean(false);
        activeSingleCancelSignal = cancelSignal;

        btnCancel.setOnClickListener(v -> {
            cancelSignal.set(true);
            safeDismissDialog(dialog);
            activeProgressDialog = null;
            activeSingleCancelSignal = null;
            Toast.makeText(MainActivity.this, "Download cancelled", Toast.LENGTH_SHORT).show();
        });

        dialog.show();

        new Thread(() -> {
            try {
                NetworkRomSource.downloadRomSync(
                        item.remoteUrl,
                        getRomsDir(),
                        item.fileName,
                        cancelSignal,
                        new NetworkRomSource.DownloadProgressListener() {
                            @Override
                            public void onProgress(long bytesRead, long totalBytes, double speedBytesPerSec) {
                                runOnUiThread(() -> {
                                    if (totalBytes > 0) {
                                        int pct = (int) Math.min(100, (bytesRead * 100) / totalBytes);
                                        pbProgress.setIndeterminate(false);
                                        pbProgress.setProgress(pct);
                                        tvPercent.setText(pct + "%");
                                        tvDetails.setText(String.format(Locale.ROOT, "%.1f MB / %.1f MB (%.1f MB/s)",
                                                bytesRead / (1024.0 * 1024.0),
                                                totalBytes / (1024.0 * 1024.0),
                                                speedBytesPerSec / (1024.0 * 1024.0)));
                                    } else {
                                        pbProgress.setIndeterminate(true);
                                        tvPercent.setText("");
                                        tvDetails.setText(String.format(Locale.ROOT, "%.1f MB (%.1f MB/s)",
                                                bytesRead / (1024.0 * 1024.0),
                                                speedBytesPerSec / (1024.0 * 1024.0)));
                                    }
                                });
                            }

                            @Override
                            public void onComplete(File file) {
                                runOnUiThread(() -> {
                                    safeDismissDialog(dialog);
                                    activeProgressDialog = null;
                                    activeSingleCancelSignal = null;
                                    item.localPath = file.getAbsolutePath();
                                    gameAdapter.notifyDataSetChanged();
                                    updateFilterCounts();

                                    if (autoLaunch) {
                                        Toast.makeText(MainActivity.this, "Download complete: " + file.getName(), Toast.LENGTH_SHORT).show();
                                        launchGameItem(item);
                                    } else {
                                        Toast.makeText(MainActivity.this, "✓ Saved " + (item.name != null ? item.name : item.fileName) + " for offline play", Toast.LENGTH_SHORT).show();
                                    }
                                });
                            }

                            @Override
                            public void onError(Exception error) {
                                runOnUiThread(() -> {
                                    safeDismissDialog(dialog);
                                    activeProgressDialog = null;
                                    activeSingleCancelSignal = null;
                                    if (!(error instanceof CancellationException)) {
                                        Toast.makeText(MainActivity.this, "Download failed: " + error.getMessage(), Toast.LENGTH_LONG).show();
                                    }
                                });
                            }
                        }
                );
            } catch (Exception e) {
                runOnUiThread(() -> {
                    safeDismissDialog(dialog);
                    activeProgressDialog = null;
                    activeSingleCancelSignal = null;
                    Toast.makeText(MainActivity.this, "Download failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    private void safeDismissDialog(AlertDialog dialog) {
        if (dialog != null && dialog.isShowing() && !isFinishing() && !isDestroyed()) {
            try {
                dialog.dismiss();
            } catch (Exception ignored) {}
        }
    }

    private void launchGameItem(GameItem item) {
        if (!item.isDownloaded(getRomsDir()) && item.remoteUrl != null) {
            startRomDownload(item, true);
            return;
        }

        Toast.makeText(this, "Preparing " + item.fileName + "...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                File romToLaunch;
                if (item.localPath != null && new File(item.localPath).exists()) {
                    romToLaunch = new File(item.localPath);
                } else if (new File(getRomsDir(), item.fileName).exists()) {
                    romToLaunch = new File(getRomsDir(), item.fileName);
                } else if (item.uri != null) {
                    File target = new File(getRomsDir(), item.fileName);
                    if (!target.exists() || target.length() == 0) {
                        copyUriToFile(item.uri, target);
                    }
                    romToLaunch = target;
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

    private void copyUriToFile(Uri uri, File target) throws Exception {
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(target)) {
            byte[] buf = new byte[32768];
            int len;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
            }
        }
    }

    private void launchDirect(String romPath) {
        File romFile = new File(romPath);
        String fileName = romFile.getName();

        String coreName;
        String systemType;
        String aspectRatio;

        String lower = fileName.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".gbc") || lower.contains(".gbc.") || lower.contains("(gbc)") || lower.contains("[gbc]")) {
            coreName = "libretro_gambatte.so";
            systemType = "GBC";
            aspectRatio = "10:9";
        } else if (lower.endsWith(".gba") || lower.contains(".gba.") || lower.contains("(gba)") || lower.contains("[gba]")) {
            coreName = "libretro_mgba.so";
            systemType = "GBA";
            aspectRatio = "3:2";
        } else if (lower.endsWith(".gb") || lower.contains(".gb.") || lower.contains("(gb)") || lower.contains("[gb]")) {
            coreName = "libretro_gambatte.so";
            systemType = "GAMEBOY";
            aspectRatio = "10:9";
        } else {
            // Heuristics for zip or untagged
            if (lower.contains("gbc") || lower.contains("color")) {
                coreName = "libretro_gambatte.so";
                systemType = "GBC";
                aspectRatio = "10:9";
            } else if (lower.contains("gba") || lower.contains("advance")) {
                coreName = "libretro_mgba.so";
                systemType = "GBA";
                aspectRatio = "3:2";
            } else {
                coreName = "libretro_gambatte.so";
                systemType = "GAMEBOY";
                aspectRatio = "10:9";
            }
        }

        Intent intent = new Intent(this, HandheldActivity.class);
        intent.putExtra("ROM_PATH", romPath);
        intent.putExtra("CORE_NAME", coreName);
        intent.putExtra("SYSTEM_TYPE", systemType);
        intent.putExtra("ASPECT_RATIO", aspectRatio);
        intent.putExtra("BIOS_DIR", getBiosDir().getAbsolutePath());
        intent.putExtra("SYSTEM_DIR", getSystemDir().getAbsolutePath());
        intent.putExtra("ROMS_DIR", getRomsDir().getAbsolutePath());
        intent.putExtra("SAVE_DIR", getSavesDir().getAbsolutePath());
        startActivity(intent);
    }

    private String getActiveServer() {
        SharedPreferences p = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        return p.getString(KEY_ACTIVE_SERVER, "");
    }

    private void setActiveServer(String url) {
        SharedPreferences p = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        p.edit().putString(KEY_ACTIVE_SERVER, url != null ? url.trim() : "").apply();
    }

    private List<String> getRecentServers() {
        SharedPreferences p = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        String raw = p.getString(KEY_RECENT_SERVERS, "");
        if (raw.isEmpty()) return Collections.emptyList();
        List<String> list = new ArrayList<>();
        for (String s : raw.split(",")) {
            String trimmed = s.trim();
            if (!trimmed.isEmpty()) list.add(trimmed);
        }
        return list;
    }

    private void addRecentServer(String url) {
        if (url == null || url.trim().isEmpty()) return;
        List<String> current = new ArrayList<>(getRecentServers());
        current.remove(url.trim());
        current.add(0, url.trim());
        while (current.size() > 5) {
            current.remove(current.size() - 1);
        }
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < current.size(); i++) {
            if (i > 0) sb.append(",");
            sb.append(current.get(i));
        }
        SharedPreferences p = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        p.edit().putString(KEY_RECENT_SERVERS, sb.toString()).apply();
    }
}
