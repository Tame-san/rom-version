#include "touch_input.h"
#include <cmath>
#include <algorithm>

namespace arcade {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

TouchInput::TouchInput() {
    current_profile_ = detect_game_profile("mslug");
    layout_ = current_profile_.layout;
    resize(1920, 1080);
}

void TouchInput::resize(int screen_width, int screen_height) {
    screen_w_ = (screen_width > 0) ? screen_width : 1920;
    screen_h_ = (screen_height > 0) ? screen_height : 1080;

    // Left Virtual Joystick / D-Pad
    dpad_default_center_x_ = screen_w_ * 0.16f;
    dpad_default_center_y_ = screen_h_ * 0.72f;
    dpad_center_x_ = dpad_default_center_x_;
    dpad_center_y_ = dpad_default_center_y_;
    dpad_knob_x_ = dpad_center_x_;
    dpad_knob_y_ = dpad_center_y_;

    dpad_base_radius_ = screen_h_ * 0.20f;
    dpad_deadzone_radius_ = screen_h_ * 0.045f;
    dpad_knob_radius_ = screen_h_ * 0.09f;

    // Top Bar buttons: COIN (SELECT), START, MENU
    topbar_buttons_.clear();

    float bar_btn_w = screen_w_ * 0.09f;
    float bar_btn_h = screen_h_ * 0.08f;
    float bar_y = screen_h_ * 0.02f;

    // COIN / CREDIT (SELECT)
    TopBarButton coin_btn;
    coin_btn.label = "COIN";
    coin_btn.retro_id = RETRO_DEVICE_ID_JOYPAD_SELECT;
    coin_btn.x = screen_w_ * 0.40f - bar_btn_w * 0.5f;
    coin_btn.y = bar_y;
    coin_btn.width = bar_btn_w;
    coin_btn.height = bar_btn_h;
    coin_btn.pressed = false;
    coin_btn.is_menu_trigger = false;
    topbar_buttons_.push_back(coin_btn);

    // START
    TopBarButton start_btn;
    start_btn.label = "START";
    start_btn.retro_id = RETRO_DEVICE_ID_JOYPAD_START;
    start_btn.x = screen_w_ * 0.60f - bar_btn_w * 0.5f;
    start_btn.y = bar_y;
    start_btn.width = bar_btn_w;
    start_btn.height = bar_btn_h;
    start_btn.pressed = false;
    start_btn.is_menu_trigger = false;
    topbar_buttons_.push_back(start_btn);

    // MENU
    TopBarButton menu_btn;
    menu_btn.label = "MENU";
    menu_btn.retro_id = 0xFFFFFFFF;
    menu_btn.x = screen_w_ * 0.93f - bar_btn_w * 0.5f;
    menu_btn.y = bar_y;
    menu_btn.width = bar_btn_w * 0.8f;
    menu_btn.height = bar_btn_h;
    menu_btn.pressed = false;
    menu_btn.is_menu_trigger = true;
    topbar_buttons_.push_back(menu_btn);

    update_layout_coordinates();
}

void TouchInput::configure_layout(const GameProfile& profile) {
    current_profile_ = profile;
    layout_ = profile.layout;
    update_layout_coordinates();
}

void TouchInput::update_layout_coordinates() {
    buttons_.clear();

    float cluster_x = screen_w_ * 0.83f;
    float cluster_y = screen_h_ * 0.68f;
    float spread_x = screen_w_ * 0.125f;
    float spread_y = screen_h_ * 0.22f;
    float base_radius = screen_h_ * 0.088f;

    for (const auto& desc : current_profile_.buttons) {
        RenderableButton btn;
        btn.x = cluster_x + desc.rel_x * spread_x;
        btn.y = cluster_y + desc.rel_y * spread_y;
        btn.radius = base_radius * desc.radius_scale;
        btn.label = desc.label;
        btn.sublabel = desc.sublabel;
        btn.color_rgba = desc.color_rgba;
        btn.retro_id = desc.retro_id;
        btn.pressed = false;
        buttons_.push_back(btn);
    }

    recompute_bitmask();
}

void TouchInput::evaluate_dpad(float x, float y) {
    float dx = x - dpad_center_x_;
    float dy = y - dpad_center_y_;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist > dpad_base_radius_) {
        // Clamp knob to boundary
        float scale = dpad_base_radius_ / dist;
        dpad_knob_x_ = dpad_center_x_ + dx * scale;
        dpad_knob_y_ = dpad_center_y_ + dy * scale;
    } else {
        dpad_knob_x_ = x;
        dpad_knob_y_ = y;
    }

    dpad_directions_ = 0;
    if (dist < dpad_deadzone_radius_) {
        // Inside deadzone: centered
        analog_x_ = 0;
        analog_y_ = 0;
        return;
    }

    // Normalized analog values (-32767 to 32767)
    float norm_dist = std::min(dist, dpad_base_radius_) / dpad_base_radius_;
    analog_x_ = static_cast<int16_t>((dx / dist) * norm_dist * 32767.0f);
    analog_y_ = static_cast<int16_t>((dy / dist) * norm_dist * 32767.0f);

    // 8-way directional quantization using angle in degrees
    // Screen coords: +Y is DOWN, +X is RIGHT
    float angle_rad = std::atan2(dy, dx);
    float angle_deg = angle_rad * 180.0f / static_cast<float>(M_PI);

    // Sectors of 45 degrees:
    // [-22.5, 22.5] -> RIGHT
    // [22.5, 67.5]  -> DOWN-RIGHT
    // [67.5, 112.5] -> DOWN
    // [112.5, 157.5]-> DOWN-LEFT
    // >= 157.5 or < -157.5 -> LEFT
    // [-157.5, -112.5] -> UP-LEFT
    // [-112.5, -67.5]  -> UP
    // [-67.5, -22.5]   -> UP-RIGHT

    if (angle_deg >= -22.5f && angle_deg < 22.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    } else if (angle_deg >= 22.5f && angle_deg < 67.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT) | (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    } else if (angle_deg >= 67.5f && angle_deg < 112.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    } else if (angle_deg >= 112.5f && angle_deg < 157.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN) | (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    } else if (angle_deg >= 157.5f || angle_deg < -157.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    } else if (angle_deg >= -157.5f && angle_deg < -112.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT) | (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    } else if (angle_deg >= -112.5f && angle_deg < -67.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    } else if (angle_deg >= -67.5f && angle_deg < -22.5f) {
        dpad_directions_ |= (1 << RETRO_DEVICE_ID_JOYPAD_UP) | (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    }
}

void TouchInput::on_touch_down(int pointer_id, float x, float y) {
    if (pointer_id < 0 || pointer_id >= MAX_POINTERS) return;

    TouchPointer& p = pointers_[pointer_id];
    p.id = pointer_id;
    p.x = x;
    p.y = y;
    p.active = true;
    p.role = PointerRole::ROLE_NONE;
    p.target_button_index = -1;

    // 1. Check Top Bar (Start, Coin, Menu)
    for (size_t i = 0; i < topbar_buttons_.size(); ++i) {
        auto& tb = topbar_buttons_[i];
        if (x >= tb.x && x <= tb.x + tb.width &&
            y >= tb.y && y <= tb.y + tb.height) {
            p.role = PointerRole::ROLE_TOPBAR;
            p.target_button_index = static_cast<int>(i);
            tb.pressed = true;
            if (tb.is_menu_trigger) {
                menu_triggered_ = true;
            }
            recompute_bitmask();
            return;
        }
    }

    // 2. Left side: Virtual D-Pad / Analog Stick
    if (x < screen_w_ * 0.45f) {
        if (!dpad_active_) {
            dpad_active_ = true;
            dpad_pointer_id_ = pointer_id;
            p.role = PointerRole::ROLE_DPAD;
            // Dynamic touch centering: anchor D-pad exactly where finger landed
            dpad_center_x_ = x;
            dpad_center_y_ = y;
            dpad_knob_x_ = x;
            dpad_knob_y_ = y;
            evaluate_dpad(x, y);
            recompute_bitmask();
            return;
        }
    }

    // 3. Right side: Dynamic Action Buttons
    if (x >= screen_w_ * 0.45f) {
        p.role = PointerRole::ROLE_BUTTON;

        // Check for 2-button combo special in beat-em-ups
        if (layout_ == LayoutType::LAYOUT_BEATEMUP_2BUTTON && buttons_.size() >= 2) {
            float mid_x = (buttons_[0].x + buttons_[1].x) * 0.5f;
            float mid_y = (buttons_[0].y + buttons_[1].y) * 0.5f;
            float combo_dist = std::sqrt((x - mid_x) * (x - mid_x) + (y - mid_y) * (y - mid_y));
            if (combo_dist < buttons_[0].radius * 0.85f) {
                // User touched combo zone between Attack and Jump!
                buttons_[0].pressed = true;
                buttons_[1].pressed = true;
                p.target_button_index = 999; // combo sentinel
                recompute_bitmask();
                return;
            }
        }

        // Standard hit test with 25% generous touch padding
        int hit_index = -1;
        float min_dist_sq = 1e9f;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            float dx = x - buttons_[i].x;
            float dy = y - buttons_[i].y;
            float dist_sq = dx * dx + dy * dy;
            float touch_r = buttons_[i].radius * 1.25f;
            if (dist_sq <= touch_r * touch_r && dist_sq < min_dist_sq) {
                min_dist_sq = dist_sq;
                hit_index = static_cast<int>(i);
            }
        }

        if (hit_index != -1) {
            p.target_button_index = hit_index;
            buttons_[hit_index].pressed = true;
        }

        recompute_bitmask();
    }
}

void TouchInput::on_touch_move(int pointer_id, float x, float y) {
    if (pointer_id < 0 || pointer_id >= MAX_POINTERS) return;

    TouchPointer& p = pointers_[pointer_id];
    if (!p.active) return;

    p.x = x;
    p.y = y;

    if (p.role == PointerRole::ROLE_DPAD && dpad_pointer_id_ == pointer_id) {
        evaluate_dpad(x, y);
        recompute_bitmask();
    } else if (p.role == PointerRole::ROLE_BUTTON) {
        // Re-evaluate button under sliding finger for fighting game motions
        for (auto& btn : buttons_) {
            btn.pressed = false;
        }

        // Recheck all active button pointers
        for (int i = 0; i < MAX_POINTERS; ++i) {
            if (pointers_[i].active && pointers_[i].role == PointerRole::ROLE_BUTTON) {
                float px = pointers_[i].x;
                float py = pointers_[i].y;

                if (layout_ == LayoutType::LAYOUT_BEATEMUP_2BUTTON && buttons_.size() >= 2) {
                    float mid_x = (buttons_[0].x + buttons_[1].x) * 0.5f;
                    float mid_y = (buttons_[0].y + buttons_[1].y) * 0.5f;
                    float combo_dist = std::sqrt((px - mid_x) * (px - mid_x) + (py - mid_y) * (py - mid_y));
                    if (combo_dist < buttons_[0].radius * 0.85f) {
                        buttons_[0].pressed = true;
                        buttons_[1].pressed = true;
                        continue;
                    }
                }

                for (auto& btn : buttons_) {
                    float dx = px - btn.x;
                    float dy = py - btn.y;
                    float touch_r = btn.radius * 1.25f;
                    if (dx * dx + dy * dy <= touch_r * touch_r) {
                        btn.pressed = true;
                    }
                }
            }
        }
        recompute_bitmask();
    }
}

void TouchInput::on_touch_up(int pointer_id) {
    if (pointer_id < 0 || pointer_id >= MAX_POINTERS) return;

    TouchPointer& p = pointers_[pointer_id];
    if (!p.active) return;

    if (p.role == PointerRole::ROLE_DPAD && dpad_pointer_id_ == pointer_id) {
        dpad_active_ = false;
        dpad_pointer_id_ = -1;
        dpad_center_x_ = dpad_default_center_x_;
        dpad_center_y_ = dpad_default_center_y_;
        dpad_knob_x_ = dpad_center_x_;
        dpad_knob_y_ = dpad_center_y_;
        dpad_directions_ = 0;
        analog_x_ = 0;
        analog_y_ = 0;
    } else if (p.role == PointerRole::ROLE_TOPBAR) {
        if (p.target_button_index >= 0 && p.target_button_index < static_cast<int>(topbar_buttons_.size())) {
            topbar_buttons_[p.target_button_index].pressed = false;
        }
    }

    p.active = false;
    p.role = PointerRole::ROLE_NONE;
    p.target_button_index = -1;

    // Refresh button pressed flags based on remaining active button pointers
    for (auto& btn : buttons_) {
        btn.pressed = false;
    }
    for (int i = 0; i < MAX_POINTERS; ++i) {
        if (pointers_[i].active && pointers_[i].role == PointerRole::ROLE_BUTTON) {
            float px = pointers_[i].x;
            float py = pointers_[i].y;
            for (auto& btn : buttons_) {
                float dx = px - btn.x;
                float dy = py - btn.y;
                float touch_r = btn.radius * 1.25f;
                if (dx * dx + dy * dy <= touch_r * touch_r) {
                    btn.pressed = true;
                }
            }
        }
    }

    recompute_bitmask();
}

void TouchInput::on_touch_cancel() {
    dpad_active_ = false;
    dpad_pointer_id_ = -1;
    dpad_center_x_ = dpad_default_center_x_;
    dpad_center_y_ = dpad_default_center_y_;
    dpad_knob_x_ = dpad_center_x_;
    dpad_knob_y_ = dpad_center_y_;
    dpad_directions_ = 0;
    analog_x_ = 0;
    analog_y_ = 0;

    for (int i = 0; i < MAX_POINTERS; ++i) {
        pointers_[i].active = false;
        pointers_[i].role = PointerRole::ROLE_NONE;
        pointers_[i].target_button_index = -1;
    }

    for (auto& btn : buttons_) {
        btn.pressed = false;
    }
    for (auto& tb : topbar_buttons_) {
        tb.pressed = false;
    }

    recompute_bitmask();
}

void TouchInput::recompute_bitmask() {
    uint16_t mask = 0;

    // 1. D-Pad directions
    mask |= dpad_directions_;

    // 2. Action buttons
    for (const auto& btn : buttons_) {
        if (btn.pressed) {
            mask |= (1 << btn.retro_id);
        }
    }

    // 3. Top bar buttons (Coin / Start)
    for (const auto& tb : topbar_buttons_) {
        if (tb.pressed && tb.retro_id != 0xFFFFFFFF) {
            mask |= (1 << tb.retro_id);
        }
    }

    joypad_bitmask_ = mask;
}

uint16_t TouchInput::get_joypad_bitmask(unsigned port) const {
    if (port == 0) {
        return joypad_bitmask_;
    }
    return 0;
}

int16_t TouchInput::get_button_state(unsigned port, unsigned id) const {
    if (port == 0) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            return static_cast<int16_t>(joypad_bitmask_);
        }
        if (id < 16) {
            return (joypad_bitmask_ & (1 << id)) ? 1 : 0;
        }
    }
    return 0;
}

int16_t TouchInput::get_analog_state(unsigned port, unsigned index, unsigned id) const {
    if (port == 0 && index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
        if (id == RETRO_DEVICE_ID_ANALOG_X) return analog_x_;
        if (id == RETRO_DEVICE_ID_ANALOG_Y) return analog_y_;
    }
    return 0;
}

bool TouchInput::check_and_clear_menu_trigger() {
    bool val = menu_triggered_;
    menu_triggered_ = false;
    return val;
}

RenderableDpad TouchInput::get_renderable_dpad() const {
    RenderableDpad dpad;
    dpad.center_x = dpad_center_x_;
    dpad.center_y = dpad_center_y_;
    dpad.knob_x = dpad_knob_x_;
    dpad.knob_y = dpad_knob_y_;
    dpad.base_radius = dpad_base_radius_;
    dpad.knob_radius = dpad_knob_radius_;
    dpad.active = dpad_active_;
    dpad.active_directions = dpad_directions_;
    return dpad;
}

const std::vector<RenderableButton>& TouchInput::get_renderable_buttons() const {
    return buttons_;
}

const std::vector<TopBarButton>& TouchInput::get_renderable_topbar() const {
    return topbar_buttons_;
}

} // namespace arcade
