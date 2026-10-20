#ifndef CORE_HOST_H__
#define CORE_HOST_H__

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "libretro.h"
#include "game_profiles.h"
#include "touch_input.h"
#include "audio_engine.h"
#include "renderer.h"

namespace arcade {

class CoreHost {
public:
    CoreHost();
    ~CoreHost();

    // Dynamically loads a Libretro core shared library (.so)
    bool load_core(const std::string& core_path);
    void unload_core();

    // Loads an arcade ROM and prepares AV pipeline
    bool load_game(const std::string& rom_path);
    void unload_game();

    // Main execution step: runs 1 frame with optional RunAhead rollback
    void step_frame();

    // Game lifecycle controls
    void reset();

    // Savestates
    bool save_state(std::vector<uint8_t>& out_state);
    bool load_state(const std::vector<uint8_t>& in_state);

    // Subsystem connections
    void set_touch_input(TouchInput* touch_input) { touch_input_ = touch_input; }
    void set_audio_engine(AudioEngine* audio_engine) { audio_engine_ = audio_engine; }
    void set_renderer(Renderer* renderer) { renderer_ = renderer; }

    // Configuration
    void set_system_directory(const std::string& dir) { system_dir_ = dir; }
    void set_save_directory(const std::string& dir) { save_dir_ = dir; }
    void set_run_ahead_frames(int frames) { run_ahead_frames_ = frames; }

    // State inspections
    bool is_core_loaded() const { return core_handle_ != nullptr; }
    bool is_game_loaded() const { return game_loaded_; }
    const GameProfile& get_active_profile() const { return active_profile_; }
    const struct retro_system_av_info& get_av_info() const { return av_info_; }
    double get_fps() const { return av_info_.timing.fps; }

    // Path validation helpers (Anti-path traversal & sandbox validation)
    static bool is_safe_rom_path(const std::string& path);
    static bool is_safe_core_path(const std::string& path);

    // Direct invocation for mock/test cores
    void set_core_api_pointers(
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
    );

private:
    // Static Libretro callbacks
    static bool cb_environment(unsigned cmd, void* data);
    static void cb_video_refresh(const void* data, unsigned width, unsigned height, size_t pitch);
    static void cb_audio_sample(int16_t left, int16_t right);
    static size_t cb_audio_sample_batch(const int16_t* data, size_t frames);
    static void cb_input_poll(void);
    static int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id);
    static void cb_log(enum retro_log_level level, const char* fmt, ...);

    // Thread-safe Host singleton instance for Libretro C callbacks (SEC-10)
    static std::atomic<CoreHost*> s_instance;

    void* core_handle_ = nullptr;
    bool game_loaded_ = false;

    // Subsystems
    TouchInput* touch_input_ = nullptr;
    AudioEngine* audio_engine_ = nullptr;
    Renderer* renderer_ = nullptr;

    // Game information
    std::string current_rom_path_;
    GameProfile active_profile_;
    struct retro_system_info system_info_{};
    struct retro_system_av_info av_info_{};

    // Paths
    std::string system_dir_ = "/sdcard/rom-version/mame/bios";
    std::string save_dir_ = "/sdcard/rom-version/saves";

    // RunAhead Rollback state
    int run_ahead_frames_ = 1;
    size_t state_size_ = 0;
    std::vector<uint8_t> canonical_state_;
    bool runahead_active_ = false;
    bool mute_audio_ = false;
    bool suppress_video_ = false;

    // Libretro API entry points
    retro_init_t fn_init_ = nullptr;
    retro_deinit_t fn_deinit_ = nullptr;
    retro_api_version_t fn_api_version_ = nullptr;
    retro_get_system_info_t fn_get_system_info_ = nullptr;
    retro_get_system_av_info_t fn_get_system_av_info_ = nullptr;
    retro_set_environment_t fn_set_environment_ = nullptr;
    retro_set_video_refresh_t fn_set_video_refresh_ = nullptr;
    retro_set_audio_sample_t fn_set_audio_sample_ = nullptr;
    retro_set_audio_sample_batch_t fn_set_audio_sample_batch_ = nullptr;
    retro_set_input_poll_t fn_set_input_poll_ = nullptr;
    retro_set_input_state_t fn_set_input_state_ = nullptr;
    retro_load_game_t fn_load_game_ = nullptr;
    retro_unload_game_t fn_unload_game_ = nullptr;
    retro_run_t fn_run_ = nullptr;
    retro_reset_t fn_reset_ = nullptr;
    retro_serialize_size_t fn_serialize_size_ = nullptr;
    retro_serialize_t fn_serialize_ = nullptr;
    retro_unserialize_t fn_unserialize_ = nullptr;
};

} // namespace arcade

#endif // CORE_HOST_H__
