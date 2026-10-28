# Project Checkpoint: Android Arcade Player & Launcher

> **Date**: 2026-10-21  
> **Status**: Verified Operational on Device (Samsung Galaxy S25 Ultra, Android 17 / One UI 9)  
> **Repository**: `/home/the-dev/repos/rom-version`  
> **Git Branch**: `master` (Clean, all fixes committed)

---

## 1. Project Overview & Target Hardware

This project is a dedicated, ultra-low-latency Android arcade player designed for retro arcade enthusiasts. It boots flagship arcade titles—starting with SNK Neo Geo games (*Metal Slug*, *King of Fighters*, *Garou*), Capcom CPS1/CPS2 (*Street Fighter II*, *Cadillacs & Dinosaurs*), and IGS PGM (*Martial Masters*)—with zero setup friction, automatic ROM/BIOS normalization, and sub-frame input response.

### Target Hardware & Environment
- **Device**: Samsung Galaxy S25 Ultra (`SM-S938B`)
- **OS**: Android 17 / Samsung One UI 9 (`ro.build.version.oneui = 90000`, API 35+)
- **SoC**: Qualcomm Snapdragon 8 Elite (Adreno 830 GPU, 120 Hz AMOLED display)
- **Audio Output**: AAudio Low-Latency Exclusive MMAP engine (`burst=96`, latency <10ms)
- **Display**: 3120x1440 (19.5:9), 4:3 arcade pillarboxed viewport (1920x1440)

---

## 2. Directory Structure

```text
/home/the-dev/repos/rom-version
├── CHECKPOINT.md                       # This comprehensive project handoff document
├── build.gradle.kts                    # Root Gradle build script
├── settings.gradle.kts                 # Root Gradle settings (includes ':app')
├── gradle.properties                   # JVM & AndroidX configurations
├── local.properties                    # SDK & NDK paths (ndk.dir=/home/the-dev/Android/Sdk/ndk/26.1.10909125)
├── gradlew                             # Gradle wrapper executable
├── app/
│   ├── build.gradle.kts                # Application module Gradle script (NDK, CMake, dependencies)
│   └── src/main/
│       ├── AndroidManifest.xml         # Manifest (MainActivity + ArcadeActivity NativeActivity, permissions)
│       ├── assets/
│       │   └── bios/                   # Bundled BIOS archives extracted automatically at startup
│       │       ├── neogeo.zip          # Neo Geo MVS/AES BIOS with all canonical & legacy aliases
│       │       ├── pgm.zip             # IGS PolyGame Master BIOS
│       │       ├── qsound.zip          # Capcom QSound audio BIOS
│       │       ├── qsound_hle.zip      # Capcom QSound HLE BIOS
│       │       ├── coh1002m.zip        # CoH BIOS
│       │       ├── neoromjb.zip        # Neo Geo Japanese BIOS
│       │       └── ngem2k.zip          # Neo Geo EM2K BIOS
│       ├── cpp/
│       │   ├── CMakeLists.txt          # CMake configuration targeting C++17 (GLESv3, EGL, aaudio, log)
│       │   ├── include/
│       │   │   ├── libretro.h          # Standard Libretro API header
│       │   │   ├── game_profiles.h     # Game database, layout configurations, and auto-detection
│       │   │   ├── touch_input.h       # Multi-touch virtual gamepad system and JIT input polling
│       │   │   ├── audio_engine.h      # AAudio low-latency engine & lock-free SPSC ring buffer
│       │   │   ├── renderer.h          # OpenGL ES 3.0 renderer (4:3 pillarbox, RGB565/XRGB8888, touch overlay)
│       │   │   └── core_host.h         # Libretro core host manager, RunAhead rollback, dynamic core loader
│       │   └── src/
│       │       ├── main.cpp            # android_main NativeActivity lifecycle & non-blocking render loop
│       │       ├── game_profiles.cpp   # Game profile definitions (Neo Geo 3/4-btn, Capcom 6-btn, Beat'em-up 2-btn)
│       │       ├── touch_input.cpp     # Touch hit-testing, dynamic D-pad center, deadzones, action buttons
│       │       ├── audio_engine.cpp    # AAudio stream lifecycle, MMAP setup, lock-free audio ring buffer
│       │       ├── renderer.cpp        # GLES3 shaders, texture upload, touch button HUD geometry rendering
│       │       └── core_host.cpp       # dlopen/dlsym for libretro_fbneo.so, retro callbacks, 1-frame RunAhead
│       ├── java/com/arcade/engine/
│       │   ├── MainActivity.java       # Launch activity: ROM browser, auto-scans /sdcard/Download/Roms, BIOS check
│       │   ├── ArcadeActivity.java     # Subclass of android.app.NativeActivity with error dialog fallback
│       │   └── RomNormalizer.java      # Dynamic on-the-fly zip inspector/normalizer injecting FBNeo canonical aliases
│       ├── jniLibs/
│       │   ├── arm64-v8a/
│       │   │   └── libretro_fbneo.so   # Pre-compiled FinalBurn Neo arcade core (64-bit ARM)
│       │   └── armeabi-v7a/
│       │       └── libretro_fbneo.so   # Pre-compiled FinalBurn Neo arcade core (32-bit ARM)
│       └── res/
│           ├── layout/
│           │   └── activity_main.xml   # Modern dark-themed Material ROM launcher UI
│           └── values/
│               ├── strings.xml         # String resources
│               └── themes.xml          # Fullscreen immersive sticky themes
├── mame/
│   ├── bios/                           # Reference master arcade BIOS archives
│   └── roms/                           # Reference master arcade ROMs (50+ titles)
├── tests/
│   ├── Makefile                        # Linux host test compilation (g++ -std=c++17)
│   ├── test_engine.cpp                 # 6 test suites verifying profiles, touch, math, audio, RunAhead, security
│   └── test_engine                     # Compiled Linux host test binary
└── worktrees/                          # Isolated git worktrees for subagents
```

---

## 3. Key Architectural Decisions & Solutions

### A. Dynamic Event Loop (`main.cpp`)
- **Root Problem Solved**: Native activities often stall on a blank screen if the event polling loop freezes. In `main.cpp`, `ALooper_pollOnce` previously checked a cached boolean `active = engine.is_initialized` that was evaluated *before* the polling loop began. When `APP_CMD_INIT_WINDOW` initialized EGL and the core, `active` remained `false`, locking `ALooper_pollOnce(-1, ...)` in `epoll_wait`.
- **Decision**: Dynamically evaluate within the polling loop:
  ```cpp
  while ((ident = ALooper_pollOnce(engine.is_initialized ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) { ... }
  ```
  This immediately converts the looper to non-blocking mode (`timeout = 0`) the moment the display initializes.

### B. FBNeo Driver Name Matching (`RomNormalizer.java`)
- **Root Problem Solved**: FBNeo resolves drivers by stripping `.zip` from the ROM archive name (e.g., `mslug.zip` -> `mslug`). If the cache file was stored as `fbneo_mslug.zip`, FBNeo looked for driver `fbneo_mslug` and failed.
- **Decision**: Store normalized ROMs inside an isolated subdirectory preserving the exact canonical driver name: `files/roms/fbneo/<rom>.zip`.
- **Decision**: Automatically copy `neogeo.zip` into `files/roms/fbneo/neogeo.zip` so FBNeo's internal parent-archive search finds the Neo Geo BIOS alongside the game.

### C. MAME 8.3 to FBNeo Hyphenated Entry Normalization
- **Root Problem Solved**: MAME ROM sets use 8.3 filenames (e.g. `201.p1`, `201.c1`), whereas FBNeo requires hyphenated names (`201-p1.p1`, `201-c1.c1`).
- **Decision**: `RomNormalizer.java` inspects the zip table in <50ms. If hyphenated aliases are missing, it clones the entries using fast deflate (`level=1`) and adds the expected aliases alongside the originals.

### D. Scoped Storage & Auto-Discovery (`MainActivity.java`)
- **Root Problem Solved**: On Android 11+ (API 30–35+), direct access to `/sdcard/Download/Roms/` is blocked by default scoped storage unless granted all-files access.
- **Decision**: Declared `MANAGE_EXTERNAL_STORAGE` and `READ_EXTERNAL_STORAGE` in `AndroidManifest.xml`. Granted via `adb shell appops set com.arcade.engine MANAGE_EXTERNAL_STORAGE allow`.
- **Decision**: `scanCommonPhonePaths()` is called during `onCreate()` and `onResume()`, automatically indexing `/sdcard/Download/Roms/` so all 50+ arcade ROMs immediately populate in the launcher list without requiring user folder picking.

### E. Low-Latency Audio Pipeline (`audio_engine.cpp`)
- **Architecture**: AAudio exclusive MMAP stream configured with `AAUDIO_PERFORMANCE_MODE_LOW_LATENCY` and `AAUDIO_SHARING_MODE_EXCLUSIVE`.
- **Ring Buffer**: Single-Producer Single-Consumer (SPSC) lock-free ring buffer connecting FBNeo's audio sample batch callback directly to the AAudio native playback thread with an ingress latency governor clamping buffer depth under 10ms.

### F. RunAhead 1-Frame Rollback (`core_host.cpp`)
- **Architecture**: For arcade games with 2-3 frames of internal hardware lag, the engine runs 1 frame ahead each tick, saving state via `retro_serialize` and rolling back via `retro_unserialize`. This cancels internal engine latency for zero input lag.

### G. Dynamic Touch Controller HUD (`touch_input.cpp` & `game_profiles.cpp`)
- **Profile Detection**: Detects game type from ROM name:
  - `LAYOUT_NEO_3BUTTON`: Metal Slug 1-5, X (Shoot [B], Jump [A], Bomb [Y]).
  - `LAYOUT_NEO_4BUTTON`: KOF, Garou, Samurai Shodown (Curved arcade A, B, C, D).
  - `LAYOUT_CAPCOM_6BUTTON`: SF2, SFA2, Darkstalkers (LP, MP, HP, LK, MK, HK grid).
  - `LAYOUT_BEATEMUP_2BUTTON`: Cadillacs & Dinosaurs, Punisher (Attack, Jump, combo zone).
- **Just-In-Time Polling**: Touch pointer positions are updated atomically; `retro_set_input_state` samples current finger positions with 0 queued frames.

### H. Monotonic Frame Pacer & 60 FPS Rate Limiter (`main.cpp` & `core_host.h`)
- **Root Problem Solved**: Samsung Galaxy S25 Ultra operates at 120Hz AMOLED. In unthrottled render loops, `eglSwapBuffers` syncs to 120Hz VSYNC, forcing 60 FPS arcade games (Neo Geo, CPS1/2) to execute at 200% speed.
- **Decision**: Implemented microsecond monotonic pacing using `std::chrono::steady_clock` and `std::this_thread::sleep_until(next_target)` tied directly to `av_info_.timing.fps` (~16,895 $\mu s$ for Neo Geo 59.1856Hz; ~16,666 $\mu s$ for 60Hz).
- **Decision**: Implemented anti-debt accumulation: lagging frames or lifecycle transitions (`APP_CMD_GAINED_FOCUS`, `APP_CMD_RESUME`) reset the baseline timestamp to avoid catch-up bursts.

### I. Glassmorphic Translucent Virtual HUD & Vector Glyphs (`renderer.cpp`)
- **Root Problem Solved**: Virtual controls previously used opaque fills ($\alpha \ge 0.75-0.95$), heavily covering gameplay, meters, and fighter art. Buttons also lacked visible labels.
- **Decision**: Converted all controls to translucent glassmorphism (D-pad fill $\alpha \approx 0.21$, action button body $\alpha \approx 0.25$ idle / $0.55$ pressed, top bar badges $\alpha \approx 0.30$ idle / $0.65$ active).
- **Decision**: Implemented vector line quad font glyphs rendering 'A', 'B', 'C', 'D' (with 'LP', 'LK', 'HP', 'HK' sublabels), and crisp icons for 'COIN' (gold ring), 'START' (play triangle), and 'MENU' (hamburger bar).

### J. Fast Root-Only ROM Normalization & Lifecycle Hardening (`RomNormalizer.java` & `AndroidManifest.xml`)
- **Root Problem Solved**: Merged MAME ROMs like `kof97.zip` contain hundreds of bootlegs in subdirectories (`kof97s72/`, `kogd/`), ballooning archive size to 874MB. Decompressing and recompressing 874MB on flash storage took 45+ seconds and stalled the UI, while `standard` launch mode caused multi-instance race conditions in FBNeo.
- **Decision**: `RomNormalizer.java` filters out subdirectories (`entry.isDirectory() || name.contains("/") || name.contains("\\")`), copying only the 13 canonical root MAME files (~35MB total). Normalization completes in $< 1$ second.
- **Decision**: Configured `ArcadeActivity` with `android:launchMode="singleTask"` to prevent concurrent activity instances from corrupting global core state.

---

## 4. Verified Commands & Workflows

### Build Environment Setup
```bash
# Set Java 17
export JAVA_HOME=/home/the-dev/.sdkman/candidates/java/17.0.20-tem
export PATH=$JAVA_HOME/bin:$PATH

# Verify SDK / NDK in local.properties
cat local.properties
# sdk.dir=/home/the-dev/Android/Sdk
# ndk.dir=/home/the-dev/Android/Sdk/ndk/26.1.10909125
```

### Build & Unit Test Verification
```bash
# 1. Run Host Linux Verification Tests (Exit code 0 required)
make -C tests

# 2. Assemble Debug APK
./gradlew assembleDebug
# Output: app/build/outputs/apk/debug/app-debug.apk (~45 MB)
```

### Deploy & Debug on Device via ADB
```bash
# 1. Install to connected device
adb install -r app/build/outputs/apk/debug/app-debug.apk

# 2. Grant all-files access
adb shell appops set com.arcade.engine MANAGE_EXTERNAL_STORAGE allow

# 3. Launch the App
adb shell am force-stop com.arcade.engine
adb shell am start -n com.arcade.engine/.MainActivity

# 4. Stream Logs
adb logcat -v time -s ArcadeEngine ArcadeCoreHost ArcadeRenderer RomNormalizer MainActivity
```

---

## 5. Roadmap & Next Enhancements for Future Sessions

When resuming in a new chat, the following enhancements are ready to be implemented:

1. **Physical Gamepad Support (Bluetooth / USB)**:
   - Handle `AInputEvent` key events (`AINPUT_EVENT_TYPE_KEY`) in `main.cpp` for Xbox, PlayStation DualSense, and 8BitDo arcade sticks.
   - Map `KEYCODE_BUTTON_A`, `KEYCODE_BUTTON_B`, `AXIS_X`, `AXIS_Y` to `RETRO_DEVICE_JOYPAD`.
   - Auto-hide virtual touch controls when a physical controller is connected.

2. **CRT Scanline / Shader Pipeline**:
   - Add a high-performance GLSL ES 3.0 CRT aperture / scanline shader in `renderer.cpp`.
   - Add a toggle in the in-game menu HUD for Crisp Pixel vs CRT Scanline simulation.

3. **In-Game Pause & Savestate UI**:
   - Expand the `MENU` button overlay in `renderer.cpp` to render an in-game modal (Save Slot 1-5, Load Slot 1-5, Reset, Exit to Launcher).

4. **Neo Geo Universe BIOS & DIP Switch Menu**:
   - Expose core options in `core_host.cpp` to toggle between MVS (Arcade) and AES (Console) mode, and enable Uni-BIOS cheat engine.

---

## 6. Security Audit & Hardening Status (Completed)

An exhaustive security audit was executed against CWE/SANS Top 25 and OWASP Mobile Top 10 standards. All identified vulnerabilities have been remediated and verified:

| Finding ID | Vulnerability Category | Mitigation Applied |
| :--- | :--- | :--- |
| **SEC-FIND-01** | Unprotected Exported Native Activity (CWE-926) | Set `android:exported="false"` on `ArcadeActivity` in [`AndroidManifest.xml`](file:///home/the-dev/repos/rom-version/app/src/main/AndroidManifest.xml#L69). |
| **SEC-FIND-02** | Path Traversal / Arbitrary File Overwrite (CWE-22, CWE-73) | Sanitized URI filenames and enforced canonical path boundary checks in [`MainActivity.java`](file:///home/the-dev/repos/rom-version/app/src/main/java/com/arcade/engine/MainActivity.java#L460-L479). |
| **SEC-FIND-03** | Unallocated Texture Upload on Resume (CWE-665, CWE-703) | Reset texture dimensions in `Renderer::shutdown` and invoke `shutdown()` during `term_display` in [`renderer.cpp`](file:///home/the-dev/repos/rom-version/app/src/main/cpp/src/renderer.cpp#L230-L235) and [`main.cpp`](file:///home/the-dev/repos/rom-version/app/src/main/cpp/src/main.cpp#L370). |
| **SEC-FIND-04** | JNI Local Reference Table Leaks (CWE-401, CWE-775) | Added `DeleteLocalRef` for `app_info_class` and `intent_class` in [`main.cpp`](file:///home/the-dev/repos/rom-version/app/src/main/cpp/src/main.cpp#L99-L138). |
| **SEC-FIND-05** | Uncontrolled Background Execution & Audio Leak (CWE-400) | Stopped AAudio stream on `APP_CMD_PAUSE` and gated frame stepping with `engine.is_initialized && engine.has_focus` in [`main.cpp`](file:///home/the-dev/repos/rom-version/app/src/main/cpp/src/main.cpp#L464-L521). |
| **SEC-FIND-06** | Missing Upper-Bound on Savestate Allocations (CWE-400, CWE-190) | Enforced 64MB upper bound `MAX_SAFE_STATE_SIZE` on serialize size in [`core_host.cpp`](file:///home/the-dev/repos/rom-version/app/src/main/cpp/src/core_host.cpp#L21-L22). |
| **SEC-FIND-07** | Directory Traversal in Native Directory Setters (CWE-22) | Rejected `..` sequences in `set_system_directory` and `set_save_directory` in [`core_host.h`](file:///home/the-dev/repos/rom-version/app/src/main/cpp/include/core_host.h#L45-L54). |
| **SEC-FIND-08** | Zip Slip Edge Cases with `.` and `..` (CWE-22) | Explicitly filtered `..` and `.` entries in `needsNormalization` and `normalizeZipEntries` in [`RomNormalizer.java`](file:///home/the-dev/repos/rom-version/app/src/main/java/com/arcade/engine/RomNormalizer.java#L125-L182). |

---

## 7. Network ROM Source Implementation (Completed)

> **Feature**: Network Address as ROM Source (Dedicated HTTPS / Local IPv4 Server)  
> **Date**: 2026-10-24  
> **Status**: Verified Operational (20 Unit Tests Passing, Debug APK Clean, Native Engine Clean)

### Architecture & Components
1. **Network Engine (`NetworkRomSource.java`)**:
   - **URL Normalization**: Supports dedicated HTTPS endpoints (`https://...`), full HTTP endpoints (`http://...`), and bare IPv4 hostnames (`192.168.1.100:8000` -> normalized to `http://192.168.1.100:8000/`).
   - **Catalog Ingestion**:
     - HTML Directory Indexing (Apache, Nginx, Python `http.server`, Caddy, Lighttpd) using linear regex matching without backtracking ReDoS.
     - JSON Catalog Indexing (string arrays, object arrays with metadata, object manifests with `files` or `roms` arrays).
     - Direct ZIP URL support for immediate single-ROM downloading and launching.
   - **Streaming Downloader**:
     - Byte tracking with speed calculation, percentage, and cancellation support via `AtomicBoolean`.
     - Atomic write via `.tmp` file move and automatic resource cleanup on failure.
     - Strict sandbox enforcement (`canonicalTarget.startsWith(canonicalDest)`).
   - **Security Hardening**:
     - Cleartext allowed for local IPv4 servers via [`network_security_config.xml`](file:///home/the-dev/repos/rom-version/app/src/main/res/xml/network_security_config.xml).
     - User CAs strictly restricted to `<debug-overrides>` to prevent release MITM.
     - HTTPS-to-HTTP redirect downgrade protection.
     - 5 MB stream ingestion upper bound preventing catalog memory exhaustion.

2. **Launcher UI Integration (`MainActivity.java`, `activity_main.xml`)**:
   - `NETWORK` action bar button alongside `SELECT ROM` and `SELECT FOLDER`.
   - Active network status card displaying connected server, game count, sync, and disconnect controls.
   - Distinct network platform badge (`bg_badge_net.xml`) in game browser.
   - Dual item cue state: `PLAY ▶` for cached/local ROMs, `GET ⬇` for remote-only titles.
   - Download progress dialog (`dialog_download_progress.xml`) with progress bar, transfer rate, and cancel action.
   - Automatic post-download flow: BIOS synchronization for `neogeo.zip` or transparent normalization and immediate launch for game ROMs.

---

## 8. Offline Download & Storage Caching Manager (Completed)

> **Feature**: Dedicated Offline Pre-Download, Storage Caching, and Library Filtering  
> **Date**: 2026-10-24  
> **Status**: Verified Operational (37 Unit Tests Passing, Debug APK Clean, Native Engine Clean)

### Architecture & Components
1. **Dual Action Controls (`item_game.xml`, `MainActivity.java`)**:
   - **`PLAY ▶`**: Direct stream/launch action. If the title is already cached locally, it launches instantly; if remote-only, it downloads and boots immediately into emulation.
   - **`⬇ SAVE`**: Dedicated offline download action. Downloads the game directly to [`getRomsDir()`](file:///home/the-dev/repos/rom-version/app/src/main/java/com/arcade/engine/MainActivity.java) **without launching the emulator**, updating the item status in real time to `✓ SAVED` and allowing users to continue queueing games.

2. **Library Filter Chips (`ALL` | `OFFLINE` | `NETWORK`)**:
   - Header filter tabs with live counters: `ALL (X)`, `OFFLINE (Y)`, `NETWORK (Z)`.
   - Dynamic switching without UI hitching via cached memory bounds checking (`isDownloadedQuick()`).
   - Automatic fallback: when disconnected from network or in airplane mode, the launcher automatically selects `OFFLINE` mode.

3. **Batch Downloader & Queue Manager ([`OfflineDownloadManager.java`](file:///home/the-dev/repos/rom-version/app/src/main/java/com/arcade/engine/OfflineDownloadManager.java))**:
   - `SAVE ALL` action in network status card computes un-downloaded game count and payload size, requests confirmation, and queues sequential background downloads.
   - Concurrency hardening using `AtomicInteger` counters (`completedCount`, `failedCount`, `currentIndex`) and `AtomicBoolean` cancellation guards.
   - Comprehensive error isolation and resource finalization ensuring `onQueueFinished` callbacks execute reliably.

4. **Storage & Cache Management (Long-Press Menu)**:
   - Long-press any game row to inspect and manage storage:
     - "Play Game"
     - "Download for Offline Play"
     - "Delete Local Copy": Safely deletes local `.zip` file, reclaims flash storage, retains remote catalog item, and updates UI state.

5. **Security Audit & Sandboxing Verifications**:
   - **SEC-01 Sandbox Enforcement**: `deleteLocalCopy` validates `romsDir != null`, sanitizes filenames, and strictly asserts `localFile.getCanonicalPath().startsWith(canonicalRomsDir)`.
   - **SEC-03 / SEC-06 Window & Lifecycle Leaks**: Dismisses dialogs via `safeDismissDialog()` guarded by `!isFinishing() && !isDestroyed()`, and cancels active queues/signals on `onDestroy()`.
   - **SEC-07 UI Thread Disk I/O Elimination**: Replaced synchronous disk `File.exists()` and `File.length()` checks during list scrolling with cached local path lookups (`isDownloadedQuick()`).

---

## 9. In-Game Pause System & Multi-Ratio Layout Spacing (Completed)

> **Feature**: In-Game Pause Modal, Android Back Key Navigation & Collision-Free Action Button Geometry  
> **Date**: 2026-10-28  
> **Status**: Verified Operational (All Host Linux Tests Passing, Debug APK Cleanly Built)

### Architecture & Solutions
1. **In-Game Pause Modal & State Engine (`TouchInput`, `main.cpp`, `Renderer`)**:
   - **Pause State Machine**: Tapping `MENU` or pressing Android `BACK` (`AKEYCODE_BACK`) pauses the game.
   - **Full Emulation Freeze**: When paused, `CoreHost::step_frame()` is suspended and `AudioEngine::stop()` halts audio playback, preventing sound loop / buzz artifacts.
   - **Glassmorphic Pause HUD (`Renderer::render_pause_menu`)**: Renders a dark backdrop veil over the frozen game frame, a cyan-bordered glassmorphic card, and three interactive vector-glyph buttons:
     - `RESUME`: Unpauses execution, restarts AAudio stream, and resets pacing baseline (`reset_pacing = true`) to prevent catch-up bursts.
     - `RESTART`: Re-initializes Libretro core via `CoreHost::reset()`, restarts audio, and resumes from frame 0.
     - `QUIT`: Cleanly unloads native resources and returns to `MainActivity`.
   - **Lifecycle Hardening**: Operating system focus loss (`APP_CMD_PAUSE`, `APP_CMD_LOST_FOCUS`) automatically sets `is_paused = true`, protecting gameplay state if interrupted by phone calls or app switching.

2. **Aspect-Ratio Safe Layout Geometry & Hitbox Separation (`touch_input.cpp`, `game_profiles.cpp`)**:
   - **The Problem Solved**: Previously, horizontal spread scaled by `screen_w_` while button radius scaled by `screen_h_`. On 16:9 and 18:9 screens, Capcom 6-button and Neo Geo 4-button layouts collided visually, and sliding finger motions triggered multiple buttons simultaneously.
   - **Unified Dimension Scaling**: Button spread and base radius now scale proportionally with `screen_h_` (`spread_x = max(screen_w * 0.125f, screen_h * 0.25f)`), while 6-button radius scale is set to `0.82f`.
   - **Zero Collision Guarantee**: Center-to-center distances exceed the sum of radii across all aspect ratios (16:9, 18:9, 19.5:9, 20:9), verified with dedicated regression tests.
   - **Single Closest Button Drag**: Dragging a finger across button clusters evaluates the single closest button hitbox rather than activating all overlapping bounding boxes.

3. **Older & Budget Device Optimization (`AudioEngine`)**:
   - **AAudio SHARED Mode Buffer Scaling**: On devices without exclusive MMAP support (budget/older chipsets), buffer capacity automatically scales to `max(burst * 4, 384 frames)`, eliminating audio underruns and crackling while maintaining low latency.

---

## 10. Targeted Multi-APK Monorepo Architecture Tracking

> **Status**: IN PROGRESS  
> **Target**: Modular Multi-System Architecture with Targeted Standalone APKs  
> **Date**: 2026-10-28  

### Module Structure & System Matrix

| Module | Type | Target Systems | Bundled Cores | Controllers / Viewports | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`:core-native`** | Android Library (C++17) | Shared Engine | Core Host, AAudio, GLES3 | Viewport calculation, Input, Pause HUD | **Complete** |
| **`:core-ui`** | Android Library (Java) | Shared Services | Network downloader, Offline cache | GameItem, Library tabs, Storage manager | **Complete** |
| **`:app-arcade`** | Application APK | SNK Neo Geo, Capcom CPS1-3, PGM | `libretro_fbneo.so` | Neo 3/4-btn, Capcom 6-btn, 4:3 Pillarbox | **Complete** |
| **`:app-handheld`**| Application APK | Game Boy, Game Boy Color, GBA | `libretro_gambatte.so`, `libretro_mgba.so` | DMG 2-button, GBA L/R, 10:9 / 3:2 | **Complete** |
| **`:app-n64`** | Application APK | Nintendo 64 (`.z64`, `.n64`, `.v64`) | `libretro_mupen64plus_next.so` | 3D Analog Stick, 4 C-buttons, Z-Trigger | **Phase 3** |

### Execution Checklist

#### Phase 1: Foundation Refactoring (Completed & Verified)
- [x] Create `:core-native` library module with CMakeLists.txt and shared C++ headers/sources (`CoreHost`, `AudioEngine`, `Renderer`, `TouchInput`, `PauseMenu`).
- [x] Make `core_path` and `aspect_ratio` dynamic (passed via Intent parameters from launcher activity).
- [x] Create `:core-ui` library module with shared Java services (`NetworkRomSource`, `OfflineDownloadManager`, `GameItem`).
- [x] Rename/refactor `:app` into `:app-arcade` depending on `:core-native` and `:core-ui`.
- [x] Remediate security audit findings: confine `CORE_PATH` and `CORE_NAME` to `nativeLibraryDir`, enforce `realpath` boundary checks, add JNI null checks.
- [x] Verify host Linux unit tests (`make -C tests`) pass with zero regressions (86 assertions, 8 suites).
- [x] Assemble `:app-arcade` Debug APK (`./gradlew :app-arcade:assembleDebug`) verified with bundled native libraries and BIOS assets.

#### Phase 2: Handheld Player Module (`:app-handheld`) (Completed & Verified)
- [x] Create `:app-handheld` module depending on `:core-native` and `:core-ui` (`com.retro.handheld`).
- [x] Bundle `libretro_gambatte.so` (GB/GBC) and `libretro_mgba.so` (GBA) in `app-handheld/src/main/jniLibs/{arm64-v8a,armeabi-v7a}/`.
- [x] Add handheld touch controller profiles (`LAYOUT_GAMEBOY` with diagonal DMG magenta B/A; `LAYOUT_GBA` with B/A and L/R shoulder triggers at 10% and 90% screen width).
- [x] Configure 10:9 (GB/GBC 160×144) and 3:2 (GBA 240×160) viewport aspect ratio modes via `parse_aspect_ratio`.
- [x] Label top bar button 0 as `"SELECT"` dynamically for console/handheld layouts while retaining `"COIN"` for arcade.
- [x] Implement modern ROM browser in `MainActivity.java` with direct cartridge loading (`.gb`, `.gbc`, `.gba`, `.zip`) and `:core-ui` offline downloader/cache.
- [x] Add Test Suite 9 to `tests/test_engine.cpp` (222 total assertions passing across 9 suites).
- [x] Complete security audit: clean pass, certified zero high/medium vulnerabilities, defense-in-depth SAF filename sanitization applied.
- [x] Assemble `:app-handheld:assembleDebug` (8.0 MB debug APK generated with verified bundled native libraries).

#### Phase 3: Nintendo 64 Player Module (`:app-n64`)
- [ ] Create `:app-n64` module depending on `:core-native` and `:core-ui`.
- [ ] Add `libretro_mupen64plus_next.so` (arm64-v8a, armeabi-v7a).
- [ ] Implement Libretro Hardware Render callback (`RETRO_ENVIRONMENT_SET_HW_RENDER`) with GLES3 FBO context sharing.
- [ ] Add N64 controller profile: 3D Analog Stick, Z-Trigger, 4-way C-button cluster, A, B, Start, L, R.
- [ ] Disable RunAhead specifically for N64 in `CoreHost` to prevent state rollback stutter.
- [ ] Verify test suite and assemble `:app-n64:assembleDebug`.



