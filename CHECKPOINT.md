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
