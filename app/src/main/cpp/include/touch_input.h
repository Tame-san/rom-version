#ifndef TOUCH_INPUT_H__
#define TOUCH_INPUT_H__

#include <cstdint>
#include <vector>
#include <string>
#include "libretro.h"
#include "game_profiles.h"

namespace arcade {

enum class PointerRole {
    ROLE_NONE,
    ROLE_DPAD,
    ROLE_BUTTON,
    ROLE_TOPBAR
};

struct TouchPointer {
    int id = -1;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
    PointerRole role = PointerRole::ROLE_NONE;
    int target_button_index = -1;
};

struct TopBarButton {
    std::string label;
    uint32_t retro_id;
    float x, y, width, height;
    bool pressed = false;
    bool is_menu_trigger = false;
};

struct RenderableButton {
    float x;
    float y;
    float radius;
    std::string label;
    std::string sublabel;
    uint32_t color_rgba;
    bool pressed;
    uint32_t retro_id;
};

struct RenderableDpad {
    float center_x;
    float center_y;
    float knob_x;
    float knob_y;
    float base_radius;
    float knob_radius;
    bool active;
    uint16_t active_directions; // Bitmask of UP, DOWN, LEFT, RIGHT
};

class TouchInput {
public:
    TouchInput();
    ~TouchInput() = default;

    // Updates screen geometry and recalculates button/D-pad positions
    void resize(int screen_width, int screen_height);

    // Configures active layout from detected GameProfile
    void configure_layout(const GameProfile& profile);

    // Multi-touch event handlers
    void on_touch_down(int pointer_id, float x, float y);
    void on_touch_move(int pointer_id, float x, float y);
    void on_touch_up(int pointer_id);
    void on_touch_cancel();

    // Just-In-Time input polling: returns joypad bitmask for player 0
    uint16_t get_joypad_bitmask(unsigned port = 0) const;

    // Checks state of a specific button id
    int16_t get_button_state(unsigned port, unsigned id) const;

    // Checks analog stick state (-32768 to 32767)
    int16_t get_analog_state(unsigned port, unsigned index, unsigned id) const;

    // Menu trigger flag (toggled by MENU topbar button)
    bool check_and_clear_menu_trigger();

    // Data getters for overlay rendering
    RenderableDpad get_renderable_dpad() const;
    const std::vector<RenderableButton>& get_renderable_buttons() const;
    const std::vector<TopBarButton>& get_renderable_topbar() const;

private:
    void update_layout_coordinates();
    void evaluate_dpad(float x, float y);
    void recompute_bitmask();

    int screen_w_ = 1920;
    int screen_h_ = 1080;

    GameProfile current_profile_;
    LayoutType layout_ = LayoutType::LAYOUT_ARCADE_4BUTTON;

    // D-Pad properties
    bool dpad_active_ = false;
    int dpad_pointer_id_ = -1;
    float dpad_default_center_x_ = 0.0f;
    float dpad_default_center_y_ = 0.0f;
    float dpad_center_x_ = 0.0f;
    float dpad_center_y_ = 0.0f;
    float dpad_knob_x_ = 0.0f;
    float dpad_knob_y_ = 0.0f;
    float dpad_base_radius_ = 140.0f;
    float dpad_deadzone_radius_ = 25.0f;
    float dpad_knob_radius_ = 65.0f;
    int16_t analog_x_ = 0;
    int16_t analog_y_ = 0;
    uint16_t dpad_directions_ = 0;

    // Buttons
    std::vector<RenderableButton> buttons_;
    std::vector<TopBarButton> topbar_buttons_;

    // Pointers tracking (up to 10 fingers)
    static constexpr int MAX_POINTERS = 10;
    TouchPointer pointers_[MAX_POINTERS];

    // JIT Joypad bitmask
    uint16_t joypad_bitmask_ = 0;
    bool menu_triggered_ = false;
};

} // namespace arcade

#endif // TOUCH_INPUT_H__
