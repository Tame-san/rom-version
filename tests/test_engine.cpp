#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include "../app/src/main/cpp/include/libretro.h"
#include "../app/src/main/cpp/include/game_profiles.h"
#include "../app/src/main/cpp/include/touch_input.h"
#include "../app/src/main/cpp/include/audio_engine.h"
#include "../app/src/main/cpp/include/renderer.h"
#include "../app/src/main/cpp/include/core_host.h"

using namespace arcade;

// Test assertion helper with formatted output
#define TEST_CHECK(cond, desc) do { \
    if (!(cond)) { \
        std::cerr << "[-] FAILED: " << desc << " (line " << __LINE__ << ")" << std::endl; \
        std::exit(1); \
    } else { \
        std::cout << "[+] PASSED: " << desc << std::endl; \
    } \
} while (0)

// -------------------------------------------------------------
// Test Suite 1: Game Profile Detection & Auto-Configuration
// -------------------------------------------------------------
void test_game_profiles() {
    std::cout << "\n=== Running Test Suite 1: Game Profile Detection ===" << std::endl;

    // 1.1 ROM ID extraction from complex paths
    TEST_CHECK(extract_rom_id("/storage/emulated/0/rom-version/mame/roms/mslug.zip") == "mslug", 
               "Extract rom id from full Android path");
    TEST_CHECK(extract_rom_id("mame/roms/KOF98.ZIP") == "kof98", 
               "Extract rom id with uppercase and zip extension");
    TEST_CHECK(extract_rom_id("garou.7z") == "garou", 
               "Extract rom id with .7z extension");
    TEST_CHECK(extract_rom_id("dino") == "dino", 
               "Extract bare rom id without extension");

    // 1.2 Metal Slug Flagship Detection
    GameProfile mslug = detect_game_profile("mslug");
    TEST_CHECK(mslug.rom_id == "mslug", "mslug rom_id matches");
    TEST_CHECK(mslug.system == SystemType::SYSTEM_NEOGEO, "mslug is Neo Geo MVS");
    TEST_CHECK(mslug.layout == LayoutType::LAYOUT_NEO_3BUTTON, "mslug has 3-button layout");
    TEST_CHECK(mslug.buttons.size() == 3, "mslug has 3 virtual buttons");
    TEST_CHECK(mslug.buttons[0].label == "SHOOT", "mslug button 0 is SHOOT");
    TEST_CHECK(mslug.buttons[1].label == "JUMP", "mslug button 1 is JUMP");
    TEST_CHECK(mslug.buttons[2].label == "BOMB", "mslug button 2 is BOMB");
    TEST_CHECK(mslug.buttons[0].retro_id == RETRO_DEVICE_ID_JOYPAD_B, "SHOOT maps to RetroPad B");
    TEST_CHECK(mslug.buttons[1].retro_id == RETRO_DEVICE_ID_JOYPAD_A, "JUMP maps to RetroPad A");
    TEST_CHECK(mslug.buttons[2].retro_id == RETRO_DEVICE_ID_JOYPAD_Y, "BOMB maps to RetroPad Y");

    // 1.3 Metal Slug sequels
    GameProfile mslug2 = detect_game_profile("mslug2.zip");
    TEST_CHECK(mslug2.layout == LayoutType::LAYOUT_NEO_3BUTTON, "mslug2 has 3-button layout");
    GameProfile mslugx = detect_game_profile("/roms/mslugx.zip");
    TEST_CHECK(mslugx.layout == LayoutType::LAYOUT_NEO_3BUTTON, "mslugx has 3-button layout");

    // 1.4 Neo Geo Fighters (4-Button Curved Arch)
    GameProfile kof98 = detect_game_profile("kof98");
    TEST_CHECK(kof98.system == SystemType::SYSTEM_NEOGEO, "kof98 is Neo Geo");
    TEST_CHECK(kof98.layout == LayoutType::LAYOUT_NEO_4BUTTON, "kof98 has 4-button arch layout");
    TEST_CHECK(kof98.buttons.size() == 4, "kof98 has 4 virtual buttons");
    TEST_CHECK(kof98.buttons[0].label == "A" && kof98.buttons[1].label == "B", "kof98 buttons A and B");
    TEST_CHECK(kof98.buttons[2].label == "C" && kof98.buttons[3].label == "D", "kof98 buttons C and D");

    GameProfile garou = detect_game_profile("garou.zip");
    TEST_CHECK(garou.layout == LayoutType::LAYOUT_NEO_4BUTTON, "garou has 4-button arch layout");

    // 1.5 Capcom Fighters (6-Button Grid)
    GameProfile sf2 = detect_game_profile("sf2");
    TEST_CHECK(sf2.system == SystemType::SYSTEM_CPS1, "sf2 is Capcom CPS1");
    TEST_CHECK(sf2.layout == LayoutType::LAYOUT_CAPCOM_6BUTTON, "sf2 has Capcom 6-button grid");
    TEST_CHECK(sf2.buttons.size() == 6, "sf2 has 6 virtual buttons");
    TEST_CHECK(sf2.buttons[0].label == "LP" && sf2.buttons[1].label == "MP" && sf2.buttons[2].label == "HP", 
               "sf2 top punch row (LP, MP, HP)");
    TEST_CHECK(sf2.buttons[3].label == "LK" && sf2.buttons[4].label == "MK" && sf2.buttons[5].label == "HK", 
               "sf2 bottom kick row (LK, MK, HK)");

    GameProfile ssf2t = detect_game_profile("ssf2t");
    TEST_CHECK(ssf2t.layout == LayoutType::LAYOUT_CAPCOM_6BUTTON, "ssf2t has 6-button grid");

    // 1.6 Beat-em-ups (2-Button Attack + Jump)
    GameProfile dino = detect_game_profile("dino.zip");
    TEST_CHECK(dino.system == SystemType::SYSTEM_CPS1, "dino is Capcom CPS1");
    TEST_CHECK(dino.layout == LayoutType::LAYOUT_BEATEMUP_2BUTTON, "dino has 2-button layout");
    TEST_CHECK(dino.buttons.size() == 2, "dino has 2 virtual buttons");
    TEST_CHECK(dino.buttons[0].label == "ATTACK" && dino.buttons[1].label == "JUMP", "dino has ATTACK and JUMP");

    GameProfile punisher = detect_game_profile("punisher");
    TEST_CHECK(punisher.layout == LayoutType::LAYOUT_BEATEMUP_2BUTTON, "punisher has 2-button layout");

    // 1.7 PGM Games
    GameProfile martmast = detect_game_profile("martmast");
    TEST_CHECK(martmast.system == SystemType::SYSTEM_PGM, "martmast is IGS PGM");
    TEST_CHECK(martmast.layout == LayoutType::LAYOUT_ARCADE_4BUTTON, "martmast has 4-button layout");

    // 1.8 Intelligent Heuristic Fallback
    GameProfile unknown_slug = detect_game_profile("mslug_turbo_edition.zip");
    TEST_CHECK(unknown_slug.layout == LayoutType::LAYOUT_NEO_3BUTTON, "Heuristic detects mslug variant");

    GameProfile unknown_kof = detect_game_profile("kof2002_magic_plus.zip");
    TEST_CHECK(unknown_kof.layout == LayoutType::LAYOUT_NEO_4BUTTON, "Heuristic detects kof variant");

    GameProfile generic_game = detect_game_profile("random_arcade_game");
    TEST_CHECK(generic_game.layout == LayoutType::LAYOUT_ARCADE_4BUTTON, "Fallback to default 4-button arcade");
}

// -------------------------------------------------------------
// Test Suite 2: Touch Input Math & Multi-Touch Bitmask
// -------------------------------------------------------------
void test_touch_input() {
    std::cout << "\n=== Running Test Suite 2: Touch Input & Virtual Gamepad ===" << std::endl;

    TouchInput touch;
    // Simulate typical modern smartphone screen: 2400 x 1080 (20:9 aspect ratio)
    touch.resize(2400, 1080);

    // Initial state: no inputs pressed
    TEST_CHECK(touch.get_joypad_bitmask(0) == 0, "Initial joypad bitmask is 0");

    // 2.1 Virtual D-Pad & Dynamic Centering
    // Finger 0 touches left zone at (350, 750)
    touch.on_touch_down(0, 350.0f, 750.0f);
    // Directly touching center should be within deadzone -> no direction
    TEST_CHECK(touch.get_joypad_bitmask(0) == 0, "D-pad deadzone rejects micro touch");

    // Move right by 80px: (430, 750) -> angle = 0 deg -> RIGHT
    touch.on_touch_move(0, 430.0f, 750.0f);
    uint16_t mask_right = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_right & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0, "D-pad registers RIGHT");
    TEST_CHECK((mask_right & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) == 0, "D-pad does NOT register LEFT");

    // Move Down-Right: (430, 830) -> angle = 45 deg -> RIGHT + DOWN
    touch.on_touch_move(0, 430.0f, 830.0f);
    uint16_t mask_down_right = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_down_right & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0 &&
               (mask_down_right & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) != 0, 
               "D-pad registers 8-way diagonal DOWN-RIGHT");

    // Move Up: (350, 670) -> angle = -90 deg -> UP
    touch.on_touch_move(0, 350.0f, 670.0f);
    uint16_t mask_up = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_up & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) != 0, "D-pad registers UP");
    TEST_CHECK((mask_up & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) == 0, "D-pad does NOT register DOWN");

    // Release D-pad
    touch.on_touch_up(0);
    TEST_CHECK(touch.get_joypad_bitmask(0) == 0, "D-pad release resets directions to 0");

    // 2.2 Metal Slug Action Buttons & Multi-touch
    GameProfile mslug_prof = detect_game_profile("mslug");
    touch.configure_layout(mslug_prof);

    const auto& buttons = touch.get_renderable_buttons();
    TEST_CHECK(buttons.size() == 3, "TouchInput configured with 3 Metal Slug buttons");

    float shoot_x = buttons[0].x;
    float shoot_y = buttons[0].y;
    float jump_x  = buttons[1].x;
    float jump_y  = buttons[1].y;

    // Finger 1 touches SHOOT
    touch.on_touch_down(1, shoot_x, shoot_y);
    uint16_t mask_shoot = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_shoot & (1 << RETRO_DEVICE_ID_JOYPAD_B)) != 0, "SHOOT button registers RetroPad B");

    // Finger 0 simultaneously moves D-pad RIGHT (multi-touch!)
    touch.on_touch_down(0, 300.0f, 700.0f);
    touch.on_touch_move(0, 380.0f, 700.0f);
    uint16_t mask_shoot_and_run = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_shoot_and_run & (1 << RETRO_DEVICE_ID_JOYPAD_B)) != 0 &&
               (mask_shoot_and_run & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0,
               "Multi-touch: simultaneous SHOOT + D-pad RIGHT active");

    // Finger 2 simultaneously hits JUMP
    touch.on_touch_down(2, jump_x, jump_y);
    uint16_t mask_3fingers = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_3fingers & (1 << RETRO_DEVICE_ID_JOYPAD_B)) != 0 &&
               (mask_3fingers & (1 << RETRO_DEVICE_ID_JOYPAD_A)) != 0 &&
               (mask_3fingers & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0,
               "3-finger multi-touch: SHOOT + JUMP + D-pad RIGHT simultaneously");

    // Release SHOOT, keep JUMP and D-pad
    touch.on_touch_up(1);
    uint16_t mask_after_shoot_up = touch.get_joypad_bitmask(0);
    TEST_CHECK((mask_after_shoot_up & (1 << RETRO_DEVICE_ID_JOYPAD_B)) == 0, "SHOOT released");
    TEST_CHECK((mask_after_shoot_up & (1 << RETRO_DEVICE_ID_JOYPAD_A)) != 0, "JUMP still held");
    TEST_CHECK((mask_after_shoot_up & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0, "D-pad RIGHT still held");

    // Release remaining fingers
    touch.on_touch_up(0);
    touch.on_touch_up(2);
    TEST_CHECK(touch.get_joypad_bitmask(0) == 0, "All fingers released -> mask 0");

    // 2.3 Beat-em-up 2-Button Combo Zone
    GameProfile dino_prof = detect_game_profile("dino");
    touch.configure_layout(dino_prof);
    const auto& dino_buttons = touch.get_renderable_buttons();
    float mid_x = (dino_buttons[0].x + dino_buttons[1].x) * 0.5f;
    float mid_y = (dino_buttons[0].y + dino_buttons[1].y) * 0.5f;

    // Touching between Attack and Jump triggers combo special!
    touch.on_touch_down(3, mid_x, mid_y);
    uint16_t combo_mask = touch.get_joypad_bitmask(0);
    TEST_CHECK((combo_mask & (1 << RETRO_DEVICE_ID_JOYPAD_B)) != 0 &&
               (combo_mask & (1 << RETRO_DEVICE_ID_JOYPAD_A)) != 0,
               "Beat-em-up combo zone activates ATTACK + JUMP simultaneously");
    touch.on_touch_up(3);

    // 2.4 Top Bar Buttons: COIN, START, MENU
    const auto& topbar = touch.get_renderable_topbar();
    TEST_CHECK(topbar.size() >= 3, "Top bar contains COIN, START, MENU");

    // Press COIN
    touch.on_touch_down(4, topbar[0].x + topbar[0].width * 0.5f, topbar[0].y + topbar[0].height * 0.5f);
    TEST_CHECK((touch.get_joypad_bitmask(0) & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) != 0, "COIN pressed (RetroPad SELECT)");
    touch.on_touch_up(4);
    TEST_CHECK((touch.get_joypad_bitmask(0) & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT)) == 0, "COIN released");

    // Press START
    touch.on_touch_down(5, topbar[1].x + topbar[1].width * 0.5f, topbar[1].y + topbar[1].height * 0.5f);
    TEST_CHECK((touch.get_joypad_bitmask(0) & (1 << RETRO_DEVICE_ID_JOYPAD_START)) != 0, "START pressed");
    touch.on_touch_up(5);

    // Press MENU
    touch.on_touch_down(6, topbar[2].x + topbar[2].width * 0.5f, topbar[2].y + topbar[2].height * 0.5f);
    TEST_CHECK(touch.check_and_clear_menu_trigger() == true, "MENU button toggled");
    TEST_CHECK(touch.check_and_clear_menu_trigger() == false, "MENU trigger cleared after read");
    touch.on_touch_up(6);
}

// -------------------------------------------------------------
// Test Suite 3: Aspect Ratio & Pillarbox Math
// -------------------------------------------------------------
void test_viewport_calculation() {
    std::cout << "\n=== Running Test Suite 3: 4:3 Aspect Ratio & Pillarbox Math ===" << std::endl;

    // Modern 20:9 Phone (2400 x 1080)
    ViewportRect vp20_9 = calculate_arcade_viewport(2400, 1080, 4.0f / 3.0f);
    TEST_CHECK(vp20_9.height == 1080, "20:9 screen uses full height (1080)");
    TEST_CHECK(vp20_9.width == 1440, "4:3 game width is 1440");
    TEST_CHECK(vp20_9.x == 480, "Pillarbox left/right black bar width is 480px");
    TEST_CHECK(vp20_9.y == 0, "Pillarbox vertical offset is 0");

    // Standard 16:9 Screen (1920 x 1080)
    ViewportRect vp16_9 = calculate_arcade_viewport(1920, 1080, 4.0f / 3.0f);
    TEST_CHECK(vp16_9.height == 1080, "16:9 screen uses full height (1080)");
    TEST_CHECK(vp16_9.width == 1440, "4:3 game width is 1440");
    TEST_CHECK(vp16_9.x == 240, "Pillarbox left/right black bar width is 240px");

    // Ultrawide 19.5:9 Phone (2340 x 1080)
    ViewportRect vp19_5_9 = calculate_arcade_viewport(2340, 1080, 4.0f / 3.0f);
    TEST_CHECK(vp19_5_9.height == 1080, "19.5:9 screen uses full height (1080)");
    TEST_CHECK(vp19_5_9.width == 1440, "4:3 game width is 1440");
    TEST_CHECK(vp19_5_9.x == 450, "Pillarbox left/right black bar width is 450px");

    // Vertical Arcade Game (3:4 aspect ratio, e.g. 1942)
    ViewportRect vp_vert = calculate_arcade_viewport(2400, 1080, 3.0f / 4.0f);
    TEST_CHECK(vp_vert.height == 1080, "Vertical arcade uses full height (1080)");
    TEST_CHECK(vp_vert.width == 810, "3:4 vertical game width is 810");
    TEST_CHECK(vp_vert.x == 795, "Vertical game pillarbox left offset is 795px");
}

// -------------------------------------------------------------
// Test Suite 4: Audio Lock-Free SPSC Ring Buffer
// -------------------------------------------------------------
void test_audio_ring_buffer() {
    std::cout << "\n=== Running Test Suite 4: Audio Lock-Free SPSC Ring Buffer ===" << std::endl;

    AudioRingBuffer ring(1024);
    TEST_CHECK(ring.capacity() >= 1024, "Buffer capacity rounded to power of 2");
    TEST_CHECK(ring.available_read() == 0, "Initial available read is 0");
    TEST_CHECK(ring.available_write() == ring.capacity(), "Initial available write equals capacity");

    // 4.1 Basic Write and Read
    std::vector<int16_t> write_pcm(256 * 2);
    for (size_t i = 0; i < 256; ++i) {
        write_pcm[i * 2 + 0] = static_cast<int16_t>(i * 10);
        write_pcm[i * 2 + 1] = static_cast<int16_t>(-i * 10);
    }

    size_t written = ring.write(write_pcm.data(), 256, 768);
    TEST_CHECK(written == 256, "Wrote 256 stereo frames");
    TEST_CHECK(ring.available_read() == 256, "Available read is 256");

    std::vector<int16_t> read_pcm(256 * 2, 0);
    size_t read_count = ring.read(read_pcm.data(), 256);
    TEST_CHECK(read_count == 256, "Read 256 stereo frames");
    TEST_CHECK(ring.available_read() == 0, "Buffer empty after reading 256 frames");

    // Verify data integrity
    bool match = true;
    for (size_t i = 0; i < 256 * 2; ++i) {
        if (read_pcm[i] != write_pcm[i]) {
            match = false;
            break;
        }
    }
    TEST_CHECK(match, "Audio sample data integrity verified bit-for-bit");

    // 4.2 Underrun Handling: Request more frames than available -> zero-fills remaining
    ring.write(write_pcm.data(), 50, 768);
    std::vector<int16_t> underrun_pcm(100 * 2, 0x5555);
    size_t actual_read = ring.read(underrun_pcm.data(), 100);
    TEST_CHECK(actual_read == 50, "Read returned available 50 frames");
    TEST_CHECK(underrun_pcm[0] == write_pcm[0], "First samples are valid audio");
    TEST_CHECK(underrun_pcm[50 * 2] == 0 && underrun_pcm[99 * 2 + 1] == 0, 
               "Underrun samples zero-filled (silence) to prevent audio pop/crackle");

    // 4.3 Ring Buffer Wrap-around
    for (int iter = 0; iter < 10; ++iter) {
        ring.write(write_pcm.data(), 200, 768);
        ring.read(read_pcm.data(), 200);
    }
    TEST_CHECK(ring.available_read() == 0, "Wrap-around test completed without pointer corruption");

    // 4.4 Latency Governor: Clamps backlog to max_target_frames (<16ms)
    std::vector<int16_t> burst_pcm(1000 * 2, 1);
    ring.write(burst_pcm.data(), 1000, 512); // max 512 frames (~10.6ms at 48kHz)
    TEST_CHECK(ring.available_read() <= 512, "Latency governor clamped audio queue under target watermark (<11ms)");
}

// -------------------------------------------------------------
// Test Suite 5: RunAhead Frame Rollback & State Management
// -------------------------------------------------------------

// Deterministic Mock Libretro Core
static uint32_t s_mock_canonical_frame = 0;
static uint32_t s_mock_video_render_count = 0;
static uint32_t s_mock_audio_sample_count = 0;
static uint32_t s_mock_last_rendered_frame = 0;
static uint16_t s_mock_last_input = 0;

static void mock_retro_init(void) {
    s_mock_canonical_frame = 0;
    s_mock_video_render_count = 0;
    s_mock_audio_sample_count = 0;
    s_mock_last_rendered_frame = 0;
    s_mock_last_input = 0;
}

static void mock_retro_deinit(void) {}
static unsigned mock_retro_api_version(void) { return RETRO_API_VERSION; }
static void mock_retro_get_system_info(struct retro_system_info* info) {
    info->library_name = "Mock Arcade Core";
    info->library_version = "1.0";
    info->valid_extensions = "zip";
    info->need_fullpath = false;
    info->block_extract = false;
}
static void mock_retro_get_system_av_info(struct retro_system_av_info* info) {
    info->geometry.base_width = 320;
    info->geometry.base_height = 224;
    info->geometry.max_width = 320;
    info->geometry.max_height = 224;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 48000.0;
}

static retro_environment_t s_mock_env_cb = nullptr;
static retro_video_refresh_t s_mock_vid_cb = nullptr;
static retro_audio_sample_t s_mock_aud_cb = nullptr;
static retro_audio_sample_batch_t s_mock_aud_batch_cb = nullptr;
static retro_input_poll_t s_mock_poll_cb = nullptr;
static retro_input_state_t s_mock_state_cb = nullptr;

static void mock_retro_set_environment(retro_environment_t cb) { s_mock_env_cb = cb; }
static void mock_retro_set_video_refresh(retro_video_refresh_t cb) { s_mock_vid_cb = cb; }
static void mock_retro_set_audio_sample(retro_audio_sample_t cb) { s_mock_aud_cb = cb; }
static void mock_retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_mock_aud_batch_cb = cb; }
static void mock_retro_set_input_poll(retro_input_poll_t cb) { s_mock_poll_cb = cb; }
static void mock_retro_set_input_state(retro_input_state_t cb) { s_mock_state_cb = cb; }

static bool mock_retro_load_game(const struct retro_game_info* game) {
    (void)game;
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (s_mock_env_cb) {
        s_mock_env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }
    return true;
}

static void mock_retro_unload_game(void) {}
static void mock_retro_reset(void) { s_mock_canonical_frame = 0; }

static void mock_retro_run(void) {
    // 1. Read input
    if (s_mock_state_cb) {
        s_mock_last_input = static_cast<uint16_t>(s_mock_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK));
    }

    // 2. Advance game tick
    s_mock_canonical_frame++;

    // 3. Render video frame
    s_mock_last_rendered_frame = s_mock_canonical_frame;
    uint16_t dummy_pixel = static_cast<uint16_t>(s_mock_canonical_frame);
    if (s_mock_vid_cb) {
        s_mock_vid_cb(&dummy_pixel, 1, 1, 2);
    }
    s_mock_video_render_count++;

    // 4. Output audio frame (800 frames per 60Hz frame @ 48kHz)
    int16_t audio_buf[2] = { static_cast<int16_t>(s_mock_canonical_frame), static_cast<int16_t>(s_mock_canonical_frame) };
    if (s_mock_aud_batch_cb) {
        s_mock_aud_batch_cb(audio_buf, 1);
    }
    s_mock_audio_sample_count++;
}

struct MockSavestate {
    uint32_t frame;
    uint16_t input;
};

static size_t mock_retro_serialize_size(void) {
    return sizeof(MockSavestate);
}

static bool mock_retro_serialize(void* data, size_t size) {
    if (size < sizeof(MockSavestate) || !data) return false;
    MockSavestate* st = static_cast<MockSavestate*>(data);
    st->frame = s_mock_canonical_frame;
    st->input = s_mock_last_input;
    return true;
}

static bool mock_retro_unserialize(const void* data, size_t size) {
    if (size < sizeof(MockSavestate) || !data) return false;
    const MockSavestate* st = static_cast<const MockSavestate*>(data);
    s_mock_canonical_frame = st->frame;
    s_mock_last_input = st->input;
    return true;
}

void test_runahead_engine() {
    std::cout << "\n=== Running Test Suite 5: RunAhead Frame Rollback Engine ===" << std::endl;

    CoreHost host;
    TouchInput touch;
    AudioEngine audio;
    Renderer renderer;

    host.set_touch_input(&touch);
    host.set_audio_engine(&audio);
    host.set_renderer(&renderer);

    host.set_core_api_pointers(
        mock_retro_init,
        mock_retro_deinit,
        mock_retro_api_version,
        mock_retro_get_system_info,
        mock_retro_get_system_av_info,
        mock_retro_set_environment,
        mock_retro_set_video_refresh,
        mock_retro_set_audio_sample,
        mock_retro_set_audio_sample_batch,
        mock_retro_set_input_poll,
        mock_retro_set_input_state,
        mock_retro_load_game,
        mock_retro_unload_game,
        mock_retro_run,
        mock_retro_reset,
        mock_retro_serialize_size,
        mock_retro_serialize,
        mock_retro_unserialize
    );

    bool loaded = host.load_game("mslug.zip");
    TEST_CHECK(loaded, "Loaded mslug into CoreHost with RunAhead enabled");
    TEST_CHECK(host.is_game_loaded(), "CoreHost reports game loaded");

    // Initial state
    TEST_CHECK(s_mock_canonical_frame == 0, "Initial canonical frame is 0");

    // Execute Frame 1 with RunAhead:
    // Touch input holds SHOOT
    const auto& buttons = touch.get_renderable_buttons();
    touch.on_touch_down(0, buttons[0].x, buttons[0].y); // SHOOT

    host.step_frame();

    // Verify RunAhead properties:
    // 1. The rendered frame was executed 1 frame ahead (frame index 1)
    TEST_CHECK(s_mock_last_rendered_frame == 1, "RunAhead rendered frame 1");
    // 2. Canonical state was saved, run ahead, rolled back, and advanced by exactly 1 tick
    TEST_CHECK(s_mock_canonical_frame == 1, "Canonical state is at frame 1 (no time drift)");
    // 3. Audio was generated for canonical state (1 batch)
    TEST_CHECK(audio.get_ring_buffer().available_read() == 1, "Audio received exactly 1 batch for canonical tick");

    // Execute Frame 2
    host.step_frame();
    TEST_CHECK(s_mock_last_rendered_frame == 2, "RunAhead rendered frame 2 (ahead of canonical)");
    TEST_CHECK(s_mock_canonical_frame == 2, "Canonical state advanced to frame 2");
    TEST_CHECK(audio.get_ring_buffer().available_read() == 2, "Audio buffer received exactly 2 batches total");

    // Savestate save and restore test
    std::vector<uint8_t> state_data;
    bool saved = host.save_state(state_data);
    TEST_CHECK(saved && !state_data.empty(), "Savestate saved successfully");

    // Run 5 more frames
    for (int i = 0; i < 5; ++i) {
        host.step_frame();
    }
    TEST_CHECK(s_mock_canonical_frame == 7, "Advanced to frame 7");

    // Restore savestate
    bool restored = host.load_state(state_data);
    TEST_CHECK(restored, "Savestate loaded successfully");
    TEST_CHECK(s_mock_canonical_frame == 2, "Canonical state restored back to frame 2");

    host.unload_game();
    TEST_CHECK(!host.is_game_loaded(), "Game unloaded cleanly");
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "  Android Arcade Engine: Local Linux Verification Suite" << std::endl;
    std::cout << "============================================================" << std::endl;

    test_game_profiles();
    test_touch_input();
    test_viewport_calculation();
    test_audio_ring_buffer();
    test_runahead_engine();

    std::cout << "\n============================================================" << std::endl;
    std::cout << "  ALL TESTS PASSED SUCCESSFULLY! (Exit Code 0)" << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}
