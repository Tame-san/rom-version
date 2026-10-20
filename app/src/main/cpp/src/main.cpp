#include <android/native_window.h>
#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

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

static int init_display(EngineContext* engine) {
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(display, attribs, &config, 1, &num_configs);

    EGLSurface surface = eglCreateWindowSurface(display, config, engine->app->window, nullptr);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGE("Unable to eglMakeCurrent");
        return -1;
    }

    EGLint w, h;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

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

    // Attempt to load standard arcade core (FBNeo)
    static const char* potential_cores[] = {
        "/data/data/com.arcade.engine/lib/libretro_fbneo.so",
        "/data/app/libretro_fbneo.so",
        "libretro_fbneo.so"
    };

    for (const char* core_path : potential_cores) {
        if (engine->core_host.load_core(core_path)) {
            LOGI("Loaded arcade core from %s", core_path);
            break;
        }
    }

    // Default flagship game search paths (Metal Slug)
    static const char* candidate_roms[] = {
        "/sdcard/rom-version/mame/roms/mslug.zip",
        "/storage/emulated/0/rom-version/mame/roms/mslug.zip",
        "/sdcard/roms/mslug.zip",
        "/sdcard/mame/roms/mslug.zip"
    };

    for (const char* rom_path : candidate_roms) {
        if (engine->core_host.load_game(rom_path)) {
            LOGI("Loaded flagship arcade ROM: %s", rom_path);
            break;
        }
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
                if (w != engine->width || h != engine->height) {
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

        // Non-blocking poll when animating, blocking when paused
        bool active = engine.is_initialized && engine.has_focus;
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

        if (active) {
            // Check menu button
            if (engine.touch_input.check_and_clear_menu_trigger()) {
                LOGI("Menu toggled");
            }

            // Step Libretro core with 1-frame RunAhead rollback
            if (engine.core_host.is_game_loaded()) {
                engine.core_host.step_frame();
            }

            // Render game and virtual controller overlay
            engine.renderer.render(engine.touch_input);

            // Present to display
            eglSwapBuffers(engine.display, engine.surface);
        }
    }
}
