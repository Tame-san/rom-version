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

    arcade::TouchInput touch_input;
    arcade::AudioEngine audio_engine;
    arcade::Renderer renderer;
    arcade::CoreHost core_host;
};

static std::string get_core_path(struct android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) {
        return "libretro_fbneo.so";
    }
    JNIEnv* env = nullptr;
    JavaVM* vm = app->activity->vm;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
        return "libretro_fbneo.so";
    }

    std::string core_path = "libretro_fbneo.so";
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
                        core_path = std::string(dir) + "/libretro_fbneo.so";
                        env->ReleaseStringUTFChars(dir_jstr, dir);
                    }
                    env->DeleteLocalRef(dir_jstr);
                }
            }
            env->DeleteLocalRef(app_info);
        }
    }
    env->DeleteLocalRef(act_class);
    vm->DetachCurrentThread();
    return core_path;
}

static std::string get_intent_string_extra(struct android_app* app, const char* extra_name) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz || !extra_name) {
        return "";
    }
    JNIEnv* env = nullptr;
    JavaVM* vm = app->activity->vm;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) {
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
            env->DeleteLocalRef(intent);
        }
    }
    env->DeleteLocalRef(act_class);
    vm->DetachCurrentThread();
    return result;
}

static void show_android_error(struct android_app* app, const char* title, const char* message) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JNIEnv* env = nullptr;
    JavaVM* vm = app->activity->vm;
    if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK && env) {
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
        vm->DetachCurrentThread();
    }
}

static void return_to_launcher(struct android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JNIEnv* env = nullptr;
    JavaVM* vm = app->activity->vm;
    if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK && env) {
        jclass act_class = env->GetObjectClass(app->activity->clazz);
        jmethodID finish_mid = env->GetMethodID(act_class, "finish", "()V");
        if (finish_mid) {
            env->CallVoidMethod(app->activity->clazz, finish_mid);
        }
        env->DeleteLocalRef(act_class);
        vm->DetachCurrentThread();
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

    // 1. Load FBNeo arcade core
    std::string core_path = get_core_path(engine->app);
    LOGI("Attempting to load arcade core from: %s", core_path.c_str());
    if (!engine->core_host.load_core(core_path)) {
        static const char* fallback_cores[] = {
            "libretro_fbneo.so",
            "/data/data/com.arcade.engine/lib/libretro_fbneo.so"
        };
        for (const char* fb : fallback_cores) {
            if (engine->core_host.load_core(fb)) {
                LOGI("Loaded fallback core from: %s", fb);
                break;
            }
        }
    }

    if (!engine->core_host.is_core_loaded()) {
        LOGE("Failed to load FBNeo arcade core!");
        show_android_error(engine->app, "Arcade Core Missing",
            "Failed to load the Libretro FBNeo arcade core library (.so).\nPlease ensure the APK was installed correctly.");
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

    if (!loaded && !intent_rom.empty()) {
        LOGE("Failed to boot arcade ROM: %s", intent_rom.c_str());
        show_android_error(engine->app, "Game Boot Notice",
            "Could not boot the selected game.\n\nFor Neo Geo games (Metal Slug, KOF, Garou), please ensure 'neogeo.zip' BIOS is in your ROMs folder.");
    }

    engine->is_initialized = true;
    return 0;
}

static void term_display(EngineContext* engine) {
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
    if (event_type == AINPUT_EVENT_TYPE_MOTION) {
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
            }
            break;

        case APP_CMD_TERM_WINDOW:
            term_display(engine);
            break;

        case APP_CMD_GAINED_FOCUS:
            engine->has_focus = true;
            break;

        case APP_CMD_LOST_FOCUS:
            engine->has_focus = false;
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

    while (true) {
        int ident;
        int events;
        struct android_poll_source* source;

        // Poll events: non-blocking when initialized, blocking when surface not yet ready
        bool active = engine.is_initialized;
        while ((ident = ALooper_pollOnce(active ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) {
                term_display(&engine);
                LOGI("Arcade Engine terminated cleanly");
                return;
            }
        }

        if (engine.is_initialized) {
            // Check menu button - return to launcher UI
            if (engine.touch_input.check_and_clear_menu_trigger()) {
                LOGI("Menu button pressed -> Returning to launcher");
                return_to_launcher(engine.app);
            }

            // Step Libretro core with 1-frame RunAhead rollback
            if (engine.core_host.is_game_loaded()) {
                engine.core_host.step_frame();
            }

            // Render game frame and virtual controller overlay
            engine.renderer.render(engine.touch_input);

            // Present to display
            eglSwapBuffers(engine.display, engine.surface);
        }
    }
}
