#include "core_host.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "ArcadeCoreHost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do { printf("[ArcadeCoreHost INFO] " __VA_ARGS__); printf("\n"); } while(0)
#define LOGW(...) do { printf("[ArcadeCoreHost WARN] " __VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { fprintf(stderr, "[ArcadeCoreHost ERROR] " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif

namespace arcade {

std::atomic<CoreHost*> CoreHost::s_instance{nullptr};

bool CoreHost::is_safe_rom_path(const std::string& path) {
    if (path.empty()) return false;
    // SEC-07: Reject directory traversal sequences
    if (path.find("..") != std::string::npos) {
        return false;
    }
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        // Direct ROM identifiers (e.g. "mslug", "kof98") are valid
        return true;
    }
    std::string ext = path.substr(dot_pos);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return (ext == ".zip" || ext == ".7z" || ext == ".bin" || ext == ".cue" || ext == ".rom");
}

CoreHost::CoreHost() {
    s_instance.store(this, std::memory_order_release);
}

CoreHost::~CoreHost() {
    unload_game();
    unload_core();
    s_instance.store(nullptr, std::memory_order_release);
}

void CoreHost::cb_log(enum retro_log_level level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    switch (level) {
        case RETRO_LOG_DEBUG: LOGI("[Core DEBUG] %s", buf); break;
        case RETRO_LOG_INFO:  LOGI("[Core INFO] %s", buf);  break;
        case RETRO_LOG_WARN:  LOGW("[Core WARN] %s", buf);  break;
        case RETRO_LOG_ERROR: LOGE("[Core ERROR] %s", buf); break;
        default:              LOGI("[Core LOG] %s", buf);   break;
    }
}

bool CoreHost::cb_environment(unsigned cmd, void* data) {
    CoreHost* host = s_instance.load(std::memory_order_acquire);
    if (!host) return false;

    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const enum retro_pixel_format* fmt = static_cast<const enum retro_pixel_format*>(data);
            if (!fmt) return false;
            if (*fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                *fmt == RETRO_PIXEL_FORMAT_XRGB8888 ||
                *fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
                if (host->renderer_) {
                    host->renderer_->set_pixel_format(*fmt);
                }
                LOGI("Pixel format set to %d", (int)*fmt);
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            *(const char**)data = host->system_dir_.c_str();
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            *(const char**)data = host->save_dir_.c_str();
            return true;
        }

        case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
            *(bool*)data = true;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: {
            *(bool*)data = true;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback* cb = static_cast<struct retro_log_callback*>(data);
            if (cb) {
                cb->log = cb_log;
                return true;
            }
            return false;
        }

        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            const struct retro_game_geometry* geom = static_cast<const struct retro_game_geometry*>(data);
            if (geom) {
                host->av_info_.geometry = *geom;
            }
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            struct retro_variable* var = static_cast<struct retro_variable*>(data);
            if (var && var->key) {
                // Return default values for typical FBNeo variables
                if (strcmp(var->key, "fbneo-neogeo-mode") == 0) {
                    var->value = "MVS";
                    return true;
                }
                if (strcmp(var->key, "fbneo-cpu-speed-adjust") == 0) {
                    var->value = "100";
                    return true;
                }
            }
            return false;
        }

        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            return true;
        }

        case RETRO_ENVIRONMENT_SHUTDOWN: {
            LOGI("Core requested shutdown");
            return true;
        }

        default:
            break;
    }

    return false;
}

void CoreHost::cb_video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    CoreHost* host = s_instance.load(std::memory_order_acquire);
    if (!host || host->suppress_video_) {
        return;
    }
    // SEC-03: Reject NULL, hardware frame buffer sentinel, and 0-dimension frames
    if (!data || data == RETRO_HW_FRAME_BUFFER_VALID || width == 0 || height == 0) {
        return;
    }
    if (host->renderer_) {
        host->renderer_->update_frame(data, width, height, pitch);
    }
}

void CoreHost::cb_audio_sample(int16_t left, int16_t right) {
    CoreHost* host = s_instance.load(std::memory_order_acquire);
    if (!host || host->mute_audio_) {
        return;
    }
    int16_t samples[2] = { left, right };
    if (host->audio_engine_) {
        host->audio_engine_->push_samples(samples, 1);
    }
}

size_t CoreHost::cb_audio_sample_batch(const int16_t* data, size_t frames) {
    CoreHost* host = s_instance.load(std::memory_order_acquire);
    if (!host || host->mute_audio_ || !data || frames == 0) {
        return frames;
    }
    if (host->audio_engine_) {
        return host->audio_engine_->push_samples(data, frames);
    }
    return frames;
}

void CoreHost::cb_input_poll(void) {
    // Touch input operates in real time with continuous JIT updating
}

int16_t CoreHost::cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    CoreHost* host = s_instance.load(std::memory_order_acquire);
    if (!host || !host->touch_input_) {
        return 0;
    }

    if (device == RETRO_DEVICE_JOYPAD) {
        return host->touch_input_->get_button_state(port, id);
    } else if (device == RETRO_DEVICE_ANALOG) {
        return host->touch_input_->get_analog_state(port, index, id);
    }

    return 0;
}

bool CoreHost::load_core(const std::string& core_path) {
    unload_core();

    LOGI("Loading Libretro core: %s", core_path.c_str());
    core_handle_ = dlopen(core_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!core_handle_) {
        LOGE("dlopen failed for core '%s': %s", core_path.c_str(), dlerror());
        return false;
    }

#define RESOLVE_SYM(name, type) \
    fn_##name##_ = reinterpret_cast<type>(dlsym(core_handle_, "retro_" #name)); \
    if (!fn_##name##_) { \
        LOGE("Symbol not found: retro_" #name); \
    }

    RESOLVE_SYM(init, retro_init_t);
    RESOLVE_SYM(deinit, retro_deinit_t);
    RESOLVE_SYM(api_version, retro_api_version_t);
    RESOLVE_SYM(get_system_info, retro_get_system_info_t);
    RESOLVE_SYM(get_system_av_info, retro_get_system_av_info_t);
    RESOLVE_SYM(set_environment, retro_set_environment_t);
    RESOLVE_SYM(set_video_refresh, retro_set_video_refresh_t);
    RESOLVE_SYM(set_audio_sample, retro_set_audio_sample_t);
    RESOLVE_SYM(set_audio_sample_batch, retro_set_audio_sample_batch_t);
    RESOLVE_SYM(set_input_poll, retro_set_input_poll_t);
    RESOLVE_SYM(set_input_state, retro_set_input_state_t);
    RESOLVE_SYM(load_game, retro_load_game_t);
    RESOLVE_SYM(unload_game, retro_unload_game_t);
    RESOLVE_SYM(run, retro_run_t);
    RESOLVE_SYM(reset, retro_reset_t);
    RESOLVE_SYM(serialize_size, retro_serialize_size_t);
    RESOLVE_SYM(serialize, retro_serialize_t);
    RESOLVE_SYM(unserialize, retro_unserialize_t);

#undef RESOLVE_SYM

    if (!fn_init_ || !fn_load_game_ || !fn_run_ || !fn_set_environment_) {
        LOGE("Core is missing essential Libretro functions");
        unload_core();
        return false;
    }

    // Connect Libretro callbacks
    fn_set_environment_(cb_environment);
    fn_set_video_refresh_(cb_video_refresh);
    fn_set_audio_sample_(cb_audio_sample);
    fn_set_audio_sample_batch_(cb_audio_sample_batch);
    fn_set_input_poll_(cb_input_poll);
    fn_set_input_state_(cb_input_state);

    fn_init_();

    if (fn_get_system_info_) {
        fn_get_system_info_(&system_info_);
        LOGI("Core initialized: %s (%s)", 
             system_info_.library_name ? system_info_.library_name : "Unknown",
             system_info_.library_version ? system_info_.library_version : "");
    }

    return true;
}

void CoreHost::unload_core() {
    if (game_loaded_) {
        unload_game();
    }

    if (core_handle_) {
        if (fn_deinit_) {
            fn_deinit_();
        }
        dlclose(core_handle_);
        core_handle_ = nullptr;
    }

    fn_init_ = nullptr;
    fn_deinit_ = nullptr;
    fn_api_version_ = nullptr;
    fn_get_system_info_ = nullptr;
    fn_get_system_av_info_ = nullptr;
    fn_set_environment_ = nullptr;
    fn_set_video_refresh_ = nullptr;
    fn_set_audio_sample_ = nullptr;
    fn_set_audio_sample_batch_ = nullptr;
    fn_set_input_poll_ = nullptr;
    fn_set_input_state_ = nullptr;
    fn_load_game_ = nullptr;
    fn_unload_game_ = nullptr;
    fn_run_ = nullptr;
    fn_reset_ = nullptr;
    fn_serialize_size_ = nullptr;
    fn_serialize_ = nullptr;
    fn_unserialize_ = nullptr;
}

void CoreHost::set_core_api_pointers(
    retro_init_t init_fn,
    retro_deinit_t deinit_fn,
    retro_api_version_t api_ver_fn,
    retro_get_system_info_t sys_info_fn,
    retro_get_system_av_info_t av_info_fn,
    retro_set_environment_t set_env_fn,
    retro_set_video_refresh_t set_vid_fn,
    retro_set_audio_sample_t set_aud_fn,
    retro_set_audio_sample_batch_t set_aud_batch_fn,
    retro_set_input_poll_t set_poll_fn,
    retro_set_input_state_t set_state_fn,
    retro_load_game_t load_game_fn,
    retro_unload_game_t unload_game_fn,
    retro_run_t run_fn,
    retro_reset_t reset_fn,
    retro_serialize_size_t ser_size_fn,
    retro_serialize_t ser_fn,
    retro_unserialize_t unser_fn
) {
    fn_init_ = init_fn;
    fn_deinit_ = deinit_fn;
    fn_api_version_ = api_ver_fn;
    fn_get_system_info_ = sys_info_fn;
    fn_get_system_av_info_ = av_info_fn;
    fn_set_environment_ = set_env_fn;
    fn_set_video_refresh_ = set_vid_fn;
    fn_set_audio_sample_ = set_aud_fn;
    fn_set_audio_sample_batch_ = set_aud_batch_fn;
    fn_set_input_poll_ = set_poll_fn;
    fn_set_input_state_ = set_state_fn;
    fn_load_game_ = load_game_fn;
    fn_unload_game_ = unload_game_fn;
    fn_run_ = run_fn;
    fn_reset_ = reset_fn;
    fn_serialize_size_ = ser_size_fn;
    fn_serialize_ = ser_fn;
    fn_unserialize_ = unser_fn;

    if (fn_set_environment_) fn_set_environment_(cb_environment);
    if (fn_set_video_refresh_) fn_set_video_refresh_(cb_video_refresh);
    if (fn_set_audio_sample_) fn_set_audio_sample_(cb_audio_sample);
    if (fn_set_audio_sample_batch_) fn_set_audio_sample_batch_(cb_audio_sample_batch);
    if (fn_set_input_poll_) fn_set_input_poll_(cb_input_poll);
    if (fn_set_input_state_) fn_set_input_state_(cb_input_state);

    if (fn_init_) fn_init_();
}

bool CoreHost::load_game(const std::string& rom_path) {
    unload_game();

    // SEC-07: Anti-path traversal check
    if (!is_safe_rom_path(rom_path)) {
        LOGE("load_game rejected unsafe ROM path: %s", rom_path.c_str());
        return false;
    }

    current_rom_path_ = rom_path;
    active_profile_ = detect_game_profile(rom_path);

    LOGI("Detected Arcade Game: '%s' (%s, %d) System: %s Layout: %s",
         active_profile_.title.c_str(),
         active_profile_.manufacturer.c_str(),
         active_profile_.year,
         system_type_to_string(active_profile_.system),
         layout_type_to_string(active_profile_.layout));

    if (touch_input_) {
        touch_input_->configure_layout(active_profile_);
    }

    run_ahead_frames_ = active_profile_.run_ahead_frames;

    if (!fn_load_game_) {
        LOGE("No retro_load_game function available");
        return false;
    }

    struct retro_game_info game_info{};
    game_info.path = current_rom_path_.c_str();
    game_info.data = nullptr;
    game_info.size = 0;
    game_info.meta = nullptr;

    if (!fn_load_game_(&game_info)) {
        LOGE("retro_load_game failed for '%s'", rom_path.c_str());
        return false;
    }

    if (fn_get_system_av_info_) {
        fn_get_system_av_info_(&av_info_);
        LOGI("Game AV Info: Resolution %ux%u (Aspect %.2f), FPS: %.2f, Sample Rate: %.0f",
             av_info_.geometry.base_width, av_info_.geometry.base_height,
             av_info_.geometry.aspect_ratio,
             av_info_.timing.fps,
             av_info_.timing.sample_rate);

        if (audio_engine_ && av_info_.timing.sample_rate > 0) {
            audio_engine_->start(static_cast<int>(av_info_.timing.sample_rate), 2);
        }
    }

    // Initialize RunAhead Savestate Buffer
    if (fn_serialize_size_) {
        state_size_ = fn_serialize_size_();
        if (state_size_ > 0) {
            canonical_state_.resize(state_size_);
            runahead_active_ = true;
            LOGI("RunAhead frame rollback enabled: State size = %zu bytes", state_size_);
        } else {
            runahead_active_ = false;
            LOGI("Core does not support serialization, RunAhead disabled");
        }
    }

    game_loaded_ = true;
    return true;
}

void CoreHost::unload_game() {
    if (game_loaded_ && fn_unload_game_) {
        fn_unload_game_();
    }
    game_loaded_ = false;
    state_size_ = 0;
    canonical_state_.clear();
    runahead_active_ = false;
}

void CoreHost::reset() {
    if (game_loaded_ && fn_reset_) {
        fn_reset_();
    }
}

void CoreHost::step_frame() {
    if (!game_loaded_ || !fn_run_) return;

    // RunAhead Frame Rollback Engine:
    // Renders 1 frame ahead to completely eliminate intrinsic game input lag!
    if (runahead_active_ && run_ahead_frames_ > 0 && fn_serialize_ && fn_unserialize_ && fn_serialize_size_) {
        // SEC-04: Dynamically query serialize size to prevent buffer overrun if core resized state
        size_t cur_sz = fn_serialize_size_();
        if (cur_sz == 0) {
            suppress_video_ = false;
            mute_audio_ = false;
            fn_run_();
            return;
        }

        if (canonical_state_.size() < cur_sz) {
            canonical_state_.resize(cur_sz);
        }
        state_size_ = cur_sz;

        // 1. Snapshot canonical state at beginning of frame
        if (!fn_serialize_(canonical_state_.data(), state_size_)) {
            LOGW("RunAhead serialize failed, falling back to direct run");
            suppress_video_ = false;
            mute_audio_ = false;
            fn_run_();
            return;
        }

        // 2. Advance 1 frame ahead with real-time input and render to screen
        suppress_video_ = false;
        mute_audio_ = true;
        fn_run_();

        // 3. Roll back to canonical baseline
        if (!fn_unserialize_(canonical_state_.data(), state_size_)) {
            LOGE("RunAhead rollback unserialize failed! Game state may be desynchronized");
        }

        // 4. Advance canonical state by 1 step for audio generation and continuous logic
        suppress_video_ = true;
        mute_audio_ = false;
        fn_run_();

        suppress_video_ = false;
        mute_audio_ = false;
    } else {
        // Standard execution
        suppress_video_ = false;
        mute_audio_ = false;
        fn_run_();
    }
}

bool CoreHost::save_state(std::vector<uint8_t>& out_state) {
    if (!game_loaded_ || !fn_serialize_size_ || !fn_serialize_) return false;
    size_t sz = fn_serialize_size_();
    if (sz == 0) return false;
    out_state.resize(sz);
    return fn_serialize_(out_state.data(), sz);
}

bool CoreHost::load_state(const std::vector<uint8_t>& in_state) {
    if (!game_loaded_ || !fn_unserialize_ || !fn_serialize_size_ || in_state.empty()) return false;
    size_t expected_sz = fn_serialize_size_();
    if (expected_sz == 0 || in_state.size() < expected_sz) {
        LOGE("load_state failed: provided buffer size (%zu) smaller than expected (%zu)",
             in_state.size(), expected_sz);
        return false;
    }
    return fn_unserialize_(in_state.data(), expected_sz);
}

} // namespace arcade
