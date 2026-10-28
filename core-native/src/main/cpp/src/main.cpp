#include <android/native_window.h>
#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>

#include <chrono>
#include <thread>
#include <string>
#include <vector>

#include "libretro.h"
#include "game_profiles.h"
#include "touch_input.h"
#include "audio_engine.h"
#include "renderer.h"
#include "core_host.h"

#define LOG_TAG "ArcadeEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct EngineContext {
    struct android_app* app = nullptr;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;

    int32_t width = 0;
    int32_t height = 0;
    bool has_focus = false;
    bool is_initialized = false;
    bool reset_pacing = true;
    bool is_paused = false;

    arcade::TouchInput touch_input;
    arcade::AudioEngine audio_engine;
    arcade::Renderer renderer;
    arcade::CoreHost core_host;
};

struct ScopedJNIEnv {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    bool attached = false;

    ScopedJNIEnv(JavaVM* java_vm) : vm(java_vm) {
        if (!vm) return;
        jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            }
        }
    }

    ~ScopedJNIEnv() {
        if (attached && vm) {
            vm->DetachCurrentThread();
        }
    }

    JNIEnv* get() const { return env; }
    explicit operator bool() const { return env != nullptr; }
};

static std::string get_intent_string_extra(struct android_app* app, const char* extra_name) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz || !extra_name) {
        return "";
    }
    ScopedJNIEnv jni(app->activity->vm);
    JNIEnv* env = jni.get();
    if (!env) {
        return "";
    }

    std::string result;
    jclass act_class = env->GetObjectClass(app->activity->clazz);
    jmethodID getIntent_mid = env->GetMethodID(act_class, "getIntent", "()Landroid/content/Intent;");
    if (getIntent_mid) {
        jobject intent = env->CallObjectMethod(app->activity->clazz, getIntent_mid);
        if (intent) {
            jclass intent_class = env->GetObjectClass(intent);
            jmethodID getStringExtra_mid = env->GetMethodID(intent_class, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
            if (getStringExtra_mid) {
                jstring key = env->NewStringUTF(extra_name);
                jstring val = (jstring)env->CallObjectMethod(intent, getStringExtra_mid, key);
                env->DeleteLocalRef(key);
                if (val) {
                    const char* str = env->GetStringUTFChars(val, nullptr);
                    if (str) {
                        result = str;
                        env->ReleaseStringUTFChars(val, str);
                    }
                    env->DeleteLocalRef(val);
                }
            }
            env->DeleteLocalRef(intent_class);
            env->DeleteLocalRef(intent);
        }
    }
    env->DeleteLocalRef(act_class);
    return result;
}

static float get_intent_float_extra(struct android_app* app, const char* extra_name, float default_val) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz || !extra_name) {
        return default_val;
    }
    ScopedJNIEnv jni(app->activity->vm);
    JNIEnv* env = jni.get();
    if (!env) {
        return default_val;
    }

    float result = default_val;
    jclass act_class = env->GetObjectClass(app->activity->clazz);
    jmethodID getIntent_mid = env->GetMethodID(act_class, "getIntent", "()Landroid/content/Intent;");
    if (getIntent_mid) {
        jobject intent = env->CallObjectMethod(app->activity->clazz, getIntent_mid);
        if (intent) {
            jclass intent_class = env->GetObjectClass(intent);
            jstring key = env->NewStringUTF(extra_name);

            // 1. Try getFloatExtra
            jmethodID getFloatExtra_mid = env->GetMethodID(intent_class, "getFloatExtra", "(Ljava/lang/String;F)F");
            if (getFloatExtra_mid) {
                jfloat val = env->CallFloatMethod(intent, getFloatExtra_mid, key, (jfloat)default_val);
                if (val != default_val && val > 0.0f) {
                    result = val;
                }
            }

            // 2. Try getStringExtra as ratio string (e.g. "4:3", "16:9", "1.3333")
            if (result == default_val) {
                jmethodID getStringExtra_mid = env->GetMethodID(intent_class, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
                if (getStringExtra_mid) {
                    jstring val = (jstring)env->CallObjectMethod(intent, getStringExtra_mid, key);
                    if (val) {
                        const char* str = env->GetStringUTFChars(val, nullptr);
                        if (str) {
                            std::string s(str);
                            if (s == "4:3" || s == "4/3") result = 4.0f / 3.0f;
                            else if (s == "16:9" || s == "16/9") result = 16.0f / 9.0f;
                            else if (s == "3:4" || s == "3/4") result = 3.0f / 4.0f;
                            else if (s == "10:9" || s == "10/9") result = 10.0f / 9.0f;
                            else if (s == "3:2" || s == "3/2") result = 3.0f / 2.0f;
                            else if (s == "8:7" || s == "8/7") result = 8.0f / 7.0f;
                            else {
                                try {
                                    float parsed = std::stof(s);
                                    if (parsed > 0.0f) result = parsed;
                                } catch (...) {}
                            }
                            env->ReleaseStringUTFChars(val, str);
                        }
                        env->DeleteLocalRef(val);
                    }
                }
            }

            env->DeleteLocalRef(key);
            env->DeleteLocalRef(intent_class);
            env->DeleteLocalRef(intent);
        }
    }
    env->DeleteLocalRef(act_class);
    return (result > 0.0f) ? result : default_val;
}

static std::string get_core_path(struct android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) {
        return "libretro_fbneo.so";
    }

    // 1. Check if CORE_PATH is explicitly set in intent
    std::string explicit_path = get_intent_string_extra(app, "CORE_PATH");
    if (!explicit_path.empty()) {
        LOGI("Using explicit CORE_PATH from Intent: %s", explicit_path.c_str());
        return explicit_path;
    }

    // 2. Check if CORE_NAME is specified, defaulting to libretro_fbneo.so
    std::string core_name = get_intent_string_extra(app, "CORE_NAME");
    if (core_name.empty()) {
        core_name = "libretro_fbneo.so";
    } else if (core_name.find(".so") == std::string::npos) {
        core_name += ".so";
    }

    ScopedJNIEnv jni(app->activity->vm);
    JNIEnv* env = jni.get();
    if (!env) {
        return core_name;
    }

    std::string core_path = core_name;
    jclass act_class = env->GetObjectClass(app->activity->clazz);
    jmethodID getAppInfo_mid = env->GetMethodID(act_class, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    if (getAppInfo_mid) {
        jobject app_info = env->CallObjectMethod(app->activity->clazz, getAppInfo_mid);
        if (app_info) {
            jclass app_info_class = env->GetObjectClass(app_info);
            jfieldID nativeLibDir_fid = env->GetFieldID(app_info_class, "nativeLibraryDir", "Ljava/lang/String;");
            if (nativeLibDir_fid) {
                jstring dir_jstr = (jstring)env->GetObjectField(app_info, nativeLibDir_fid);
                if (dir_jstr) {
                    const char* dir = env->GetStringUTFChars(dir_jstr, nullptr);
                    if (dir) {
                        core_path = std::string(dir) + "/" + core_name;
                        env->ReleaseStringUTFChars(dir_jstr, dir);
                    }
                    env->DeleteLocalRef(dir_jstr);
                }
            }
            env->DeleteLocalRef(app_info_class);
            env->DeleteLocalRef(app_info);
        }
    }
    env->DeleteLocalRef(act_class);
    return core_path;
}

static void show_android_error(struct android_app* app, const char* title, const char* message) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    ScopedJNIEnv jni(app->activity->vm);
    JNIEnv* env = jni.get();
    if (env) {
        jclass act_class = env->GetObjectClass(app->activity->clazz);
        jmethodID showFatalError_mid = env->GetMethodID(act_class, "showFatalError", "(Ljava/lang/String;Ljava/lang/String;)V");
        if (showFatalError_mid) {
            jstring jtitle = env->NewStringUTF(title);
            jstring jmsg = env->NewStringUTF(message);
            env->CallVoidMethod(app->activity->clazz, showFatalError_mid, jtitle, jmsg);
            env->DeleteLocalRef(jtitle);
            env->DeleteLocalRef(jmsg);
        }
        env->DeleteLocalRef(act_class);
    }
}

static void return_to_launcher(struct android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    ScopedJNIEnv jni(app->activity->vm);
    JNIEnv* env = jni.get();
    if (env) {
        jclass act_class = env->GetObjectClass(app->activity->clazz);
        jmethodID finish_mid = env->GetMethodID(act_class, "finish", "()V");
        if (finish_mid) {
            env->CallVoidMethod(app->activity->clazz, finish_mid);
        }
        env->DeleteLocalRef(act_class);
    }
}

static int init_display(EngineContext* engine) {
    if (!engine || !engine->app || !engine->app->window) {
        LOGE("init_display called without valid window");
        return -1;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return -1;
    }

    if (!eglInitialize(display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return -1;
    }

    EGLConfig config = nullptr;
    EGLint num_configs = 0;
    eglChooseConfig(display, attribs, &config, 1, &num_configs);
    if (num_configs <= 0 || config == nullptr) {
        LOGW("Exact EGL attribs returned 0 configs, falling back to relaxed configuration");
        const EGLint fallback_attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE
        };
        eglChooseConfig(display, fallback_attribs, &config, 1, &num_configs);
    }

    if (num_configs <= 0 || config == nullptr) {
        LOGE("Unable to find suitable EGL config");
        return -1;
    }

    // Configure window buffer geometry based on chosen visual ID (required on Samsung One UI)
    EGLint visual_id = 0;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &visual_id);
    ANativeWindow_setBuffersGeometry(engine->app->window, 0, 0, visual_id);

    EGLSurface surface = eglCreateWindowSurface(display, config, engine->app->window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return -1;
    }

    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        eglDestroySurface(display, surface);
        return -1;
    }

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGE("Unable to eglMakeCurrent: 0x%x", eglGetError());
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        return -1;
    }

    EGLint w = 0, h = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    if (w <= 0 || h <= 0) {
        w = 1920;
        h = 1080;
    }

    engine->display = display;
    engine->surface = surface;
    engine->context = context;
    engine->width = w;
    engine->height = h;

    // Enable VSync for smooth 60fps tearing-free arcade gameplay
    eglSwapInterval(display, 1);

    // Initialize subsystems
    engine->renderer.init(w, h);
    engine->touch_input.resize(w, h);
    engine->core_host.set_touch_input(&engine->touch_input);
    engine->core_host.set_audio_engine(&engine->audio_engine);
    engine->core_host.set_renderer(&engine->renderer);

    // Dynamic Aspect Ratio: inspect Intent extras (defaults to 1.3333f 4:3 if not specified)
    float target_aspect_ratio = get_intent_float_extra(engine->app, "ASPECT_RATIO", 4.0f / 3.0f);
    engine->renderer.set_aspect_ratio(target_aspect_ratio);

    // 1. Load Libretro core (dynamic from intent CORE_NAME / CORE_PATH, default libretro_fbneo.so)
    std::string core_path = get_core_path(engine->app);
    std::string core_name = get_intent_string_extra(engine->app, "CORE_NAME");
    if (core_name.empty()) {
        core_name = "libretro_fbneo.so";
    } else if (core_name.find(".so") == std::string::npos) {
        core_name += ".so";
    }

    LOGI("Attempting to load retro core from: %s", core_path.c_str());
    if (!engine->core_host.load_core(core_path)) {
        std::vector<std::string> fallback_cores = {
            core_name,
            "/data/data/com.arcade.engine/lib/" + core_name,
            "libretro_fbneo.so",
            "/data/data/com.arcade.engine/lib/libretro_fbneo.so"
        };
        for (const auto& fb : fallback_cores) {
            if (engine->core_host.load_core(fb)) {
                LOGI("Loaded fallback core from: %s", fb.c_str());
                break;
            }
        }
    }

    if (!engine->core_host.is_core_loaded()) {
        LOGE("Failed to load Libretro core: %s", core_path.c_str());
        show_android_error(engine->app, "Core Missing",
            "Failed to load the Libretro core library (.so).\nPlease ensure the APK was installed correctly.");
    }

    // 2. Configure System and Save Directories from Intent
    std::string bios_dir = get_intent_string_extra(engine->app, "BIOS_DIR");
    std::string system_dir = get_intent_string_extra(engine->app, "SYSTEM_DIR");
    std::string save_dir = get_intent_string_extra(engine->app, "SAVE_DIR");

    if (!bios_dir.empty()) {
        engine->core_host.set_system_directory(bios_dir);
    } else if (!system_dir.empty()) {
        engine->core_host.set_system_directory(system_dir);
    }

    if (!save_dir.empty()) {
        engine->core_host.set_save_directory(save_dir);
    }

    // 3. Load ROM: Check launching Intent first
    std::string intent_rom = get_intent_string_extra(engine->app, "ROM_PATH");
    bool loaded = false;
    if (!intent_rom.empty()) {
        LOGI("Found ROM_PATH from launching Intent: %s", intent_rom.c_str());
        if (engine->core_host.get_system_directory().empty()) {
            size_t slash = intent_rom.find_last_of("/\\");
            if (slash != std::string::npos) {
                engine->core_host.set_system_directory(intent_rom.substr(0, slash));
            }
        }
        loaded = engine->core_host.load_game(intent_rom);
        if (loaded) {
            LOGI("Loaded ROM successfully: %s", intent_rom.c_str());
        } else {
            LOGE("retro_load_game failed for Intent ROM: %s", intent_rom.c_str());
        }
    }

    // 4. If not loaded via intent, check common phone directories for flagship Metal Slug
    if (!loaded) {
        static const char* candidate_roms[] = {
            "/sdcard/roms/mslug.zip",
            "/storage/emulated/0/roms/mslug.zip",
            "/sdcard/ROMs/mslug.zip",
            "/storage/emulated/0/ROMs/mslug.zip",
            "/sdcard/Download/roms/mslug.zip",
            "/storage/emulated/0/Download/roms/mslug.zip",
            "/sdcard/Download/mslug.zip",
            "/storage/emulated/0/Download/mslug.zip",
            "/sdcard/MAME4droid/roms/mslug.zip",
            "/storage/emulated/0/MAME4droid/roms/mslug.zip",
            "/sdcard/mame/roms/mslug.zip",
            "/storage/emulated/0/mame/roms/mslug.zip"
        };

        for (const char* rom_path : candidate_roms) {
            if (engine->core_host.load_game(rom_path)) {
                loaded = true;
                LOGI("Loaded flagship arcade ROM: %s", rom_path);
                break;
            }
        }
    }

    // 5. Inspect SYSTEM_TYPE or PROFILE_NAME extras
    std::string system_type_extra = get_intent_string_extra(engine->app, "SYSTEM_TYPE");
    std::string profile_name_extra = get_intent_string_extra(engine->app, "PROFILE_NAME");
    std::string profile_key = !profile_name_extra.empty() ? profile_name_extra : system_type_extra;
    if (!profile_key.empty()) {
        LOGI("Applying custom SYSTEM_TYPE / PROFILE_NAME from Intent: %s", profile_key.c_str());
        arcade::GameProfile custom_profile = arcade::detect_game_profile(profile_key);
        engine->core_host.set_active_profile(custom_profile);
        engine->touch_input.configure_layout(custom_profile);
    }

    // 6. Explicit ASPECT_RATIO override if provided in intent
    std::string explicit_ar_extra = get_intent_string_extra(engine->app, "ASPECT_RATIO");
    if (!explicit_ar_extra.empty() && target_aspect_ratio > 0.0f) {
        engine->renderer.set_aspect_ratio(target_aspect_ratio);
    }

    if (!loaded && !intent_rom.empty()) {
        LOGE("Failed to boot arcade ROM: %s", intent_rom.c_str());
        show_android_error(engine->app, "Game Boot Notice",
            "Could not boot the selected game.\n\nFor Neo Geo games (Metal Slug, KOF, Garou), please ensure 'neogeo.zip' BIOS is in your ROMs folder.");
    }

    engine->is_initialized = true;
    return 0;
}

static void term_display(EngineContext* engine) {
    if (!engine) return;
    engine->renderer.shutdown();
    if (engine->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (engine->context != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->display, engine->context);
        }
        if (engine->surface != EGL_NO_SURFACE) {
            eglDestroySurface(engine->display, engine->surface);
        }
        eglTerminate(engine->display);
    }

    engine->display = EGL_NO_DISPLAY;
    engine->context = EGL_NO_CONTEXT;
    engine->surface = EGL_NO_SURFACE;
    engine->is_initialized = false;
}

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    EngineContext* engine = static_cast<EngineContext*>(app->userData);
    if (!engine) return 0;

    int32_t event_type = AInputEvent_getType(event);
    if (event_type == AINPUT_EVENT_TYPE_KEY) {
        int32_t key_code = AKeyEvent_getKeyCode(event);
        int32_t key_action = AKeyEvent_getAction(event);
        if (key_code == AKEYCODE_BACK && key_action == AKEY_EVENT_ACTION_UP) {
            if (!engine->is_paused) {
                engine->is_paused = true;
                engine->touch_input.set_paused(true);
                engine->audio_engine.stop();
                engine->reset_pacing = true;
            } else {
                return_to_launcher(engine->app);
            }
            return 1;
        }
    } else if (event_type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t action_masked = action & AMOTION_EVENT_ACTION_MASK;
        int32_t pointer_index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        int32_t pointer_id = AMotionEvent_getPointerId(event, pointer_index);

        switch (action_masked) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                float x = AMotionEvent_getX(event, pointer_index);
                float y = AMotionEvent_getY(event, pointer_index);
                engine->touch_input.on_touch_down(pointer_id, x, y);
                return 1;
            }

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP: {
                engine->touch_input.on_touch_up(pointer_id);
                return 1;
            }

            case AMOTION_EVENT_ACTION_MOVE: {
                size_t count = AMotionEvent_getPointerCount(event);
                for (size_t i = 0; i < count; ++i) {
                    int32_t pid = AMotionEvent_getPointerId(event, i);
                    float px = AMotionEvent_getX(event, i);
                    float py = AMotionEvent_getY(event, i);
                    engine->touch_input.on_touch_move(pid, px, py);
                }
                return 1;
            }

            case AMOTION_EVENT_ACTION_CANCEL: {
                engine->touch_input.on_touch_cancel();
                return 1;
            }
        }
    }
    return 0;
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    EngineContext* engine = static_cast<EngineContext*>(app->userData);
    if (!engine) return;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (engine->app->window != nullptr) {
                init_display(engine);
                engine->reset_pacing = true;
            }
            break;

        case APP_CMD_TERM_WINDOW:
            term_display(engine);
            engine->reset_pacing = true;
            break;

        case APP_CMD_GAINED_FOCUS:
            engine->has_focus = true;
            engine->reset_pacing = true;
            break;

        case APP_CMD_LOST_FOCUS:
            engine->has_focus = false;
            engine->reset_pacing = true;
            break;

        case APP_CMD_PAUSE:
            engine->has_focus = false;
            engine->is_paused = true;
            engine->touch_input.set_paused(true);
            engine->audio_engine.stop();
            engine->reset_pacing = true;
            break;

        case APP_CMD_RESUME:
            if (!engine->is_paused && engine->core_host.is_game_loaded() && !engine->audio_engine.is_running()) {
                engine->audio_engine.start(engine->audio_engine.get_sample_rate(), 2);
            }
            engine->reset_pacing = true;
            break;

        case APP_CMD_CONFIG_CHANGED: {
            if (engine->display != EGL_NO_DISPLAY && engine->surface != EGL_NO_SURFACE) {
                EGLint w, h;
                eglQuerySurface(engine->display, engine->surface, EGL_WIDTH, &w);
                eglQuerySurface(engine->display, engine->surface, EGL_HEIGHT, &h);
                if (w > 0 && h > 0 && (w != engine->width || h != engine->height)) {
                    engine->width = w;
                    engine->height = h;
                    engine->renderer.on_surface_changed(w, h);
                    engine->touch_input.resize(w, h);
                }
            }
            break;
        }
    }
}

void android_main(struct android_app* app) {
    EngineContext engine;
    engine.app = app;
    app->userData = &engine;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    LOGI("Ultra-Low-Latency Android Arcade Engine starting");

    std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();

    while (true) {
        int ident;
        int events;
        struct android_poll_source* source;

        // Poll events: non-blocking when initialized & focused, blocking when surface not yet ready or unfocused
        while ((ident = ALooper_pollOnce((engine.is_initialized && engine.has_focus) ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) {
                term_display(&engine);
                LOGI("Arcade Engine terminated cleanly");
                return;
            }
        }

        if (engine.is_initialized && engine.has_focus) {
            // 1. Check Menu button: toggle Pause Menu
            if (engine.touch_input.check_and_clear_menu_trigger()) {
                engine.is_paused = !engine.is_paused;
                engine.touch_input.set_paused(engine.is_paused);
                if (engine.is_paused) {
                    LOGI("Menu button pressed -> Pausing game");
                    engine.audio_engine.stop();
                } else {
                    LOGI("Menu button pressed -> Resuming game");
                    if (engine.core_host.is_game_loaded() && !engine.audio_engine.is_running()) {
                        engine.audio_engine.start(engine.audio_engine.get_sample_rate(), 2);
                    }
                    engine.reset_pacing = true;
                }
            }

            // 2. Check interactive Pause Menu actions
            if (engine.is_paused) {
                arcade::PauseAction action = engine.touch_input.check_and_clear_pause_action();
                if (action == arcade::PauseAction::RESUME) {
                    LOGI("Pause Menu: RESUME selected");
                    engine.is_paused = false;
                    engine.touch_input.set_paused(false);
                    if (engine.core_host.is_game_loaded() && !engine.audio_engine.is_running()) {
                        engine.audio_engine.start(engine.audio_engine.get_sample_rate(), 2);
                    }
                    engine.reset_pacing = true;
                } else if (action == arcade::PauseAction::RESTART) {
                    LOGI("Pause Menu: RESTART selected");
                    engine.core_host.reset();
                    engine.is_paused = false;
                    engine.touch_input.set_paused(false);
                    if (engine.core_host.is_game_loaded() && !engine.audio_engine.is_running()) {
                        engine.audio_engine.start(engine.audio_engine.get_sample_rate(), 2);
                    }
                    engine.reset_pacing = true;
                } else if (action == arcade::PauseAction::QUIT) {
                    LOGI("Pause Menu: QUIT selected -> Returning to launcher");
                    return_to_launcher(engine.app);
                }
            }

            // Obtain target FPS from engine.core_host.get_system_av_info().timing.fps (default to 60.0 if <= 0)
            double fps = engine.core_host.get_system_av_info().timing.fps;
            if (fps <= 0.0) {
                fps = 60.0;
            }
            int64_t target_frame_time_us = static_cast<int64_t>(1000000.0 / fps);
            std::chrono::microseconds target_frame_interval(target_frame_time_us);

            auto frame_start = std::chrono::steady_clock::now();
            if (engine.reset_pacing) {
                last_frame_time = frame_start;
                engine.reset_pacing = false;
            }

            // Step Libretro core with 1-frame RunAhead rollback only when not paused
            if (!engine.is_paused && engine.core_host.is_game_loaded()) {
                engine.core_host.step_frame();
            }

            // Render game frame and virtual controller or pause overlay
            engine.renderer.render(engine.touch_input);

            // Present to display
            eglSwapBuffers(engine.display, engine.surface);

            // Measure frame processing time and sleep the remaining time
            auto frame_end = std::chrono::steady_clock::now();
            auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start);
            (void)frame_duration;

            auto next_target = last_frame_time + target_frame_interval;
            if (frame_end < next_target) {
                std::this_thread::sleep_until(next_target);
                last_frame_time = next_target;
            } else {
                // Prevent frame debt accumulation: if the frame took longer than target
                // (or after app pauses/window focus loss), reset the baseline timestamp
                // rather than running rapid catch-up frames.
                last_frame_time = frame_end;
            }
        }
    }
}
