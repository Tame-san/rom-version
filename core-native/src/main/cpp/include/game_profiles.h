#ifndef GAME_PROFILES_H__
#define GAME_PROFILES_H__

#include <string>
#include <vector>
#include <cstdint>
#include "libretro.h"

namespace arcade {

enum class LayoutType {
    LAYOUT_NEO_3BUTTON,      // Metal Slug: Shoot [A], Jump [B], Bomb [C]
    LAYOUT_NEO_4BUTTON,      // Neo Geo fighters (KOF, Garou, Last Blade, SamSho): A, B, C, D curved arch
    LAYOUT_CAPCOM_6BUTTON,   // Capcom fighters (SF2, Darkstalkers, Marvel): 3 punches top, 3 kicks bottom
    LAYOUT_BEATEMUP_2BUTTON, // Beat-em-ups (Cadillacs & Dinosaurs, Punisher): Attack, Jump (+ combo zone)
    LAYOUT_ARCADE_4BUTTON    // Generic 4-button arcade diamond/arch fallback
};

enum class SystemType {
    SYSTEM_NEOGEO,
    SYSTEM_CPS1,
    SYSTEM_CPS2,
    SYSTEM_CPS3,
    SYSTEM_PGM,
    SYSTEM_KONAMI,
    SYSTEM_MIDWAY,
    SYSTEM_ARCADE_GENERIC
};

// Virtual button descriptor for rendering and hit-testing
struct VirtualButtonDesc {
    std::string label;       // Primary label (e.g. "SHOOT", "JUMP", "BOMB", "LP")
    std::string sublabel;    // Sub label (e.g. "A", "B", "C", "D")
    uint32_t retro_id;       // RETRO_DEVICE_ID_JOYPAD_* mapping
    float rel_x;             // Normalized offset within button cluster [-1.0 .. 1.0]
    float rel_y;             // Normalized offset within button cluster [-1.0 .. 1.0]
    float radius_scale;      // Scaling relative to standard button radius
    uint32_t color_rgba;     // RGBA color code for button visual theme
};

struct GameProfile {
    std::string rom_id;           // Canonical ROM base name (e.g. "mslug", "garou", "sf2")
    std::string title;            // Full display title
    std::string manufacturer;     // e.g. "SNK", "Capcom", "IGS"
    int year;                     // Release year
    SystemType system;
    LayoutType layout;
    int run_ahead_frames;         // RunAhead rollback frame count (1-2) to cancel input lag
    std::vector<VirtualButtonDesc> buttons;
};

// Extracts canonical ROM ID from a path or filename (e.g. "/path/to/mslug2.zip" -> "mslug2")
std::string extract_rom_id(const std::string& rom_path_or_name);

// Returns system name as a printable string
const char* system_type_to_string(SystemType system);

// Returns layout name as a printable string
const char* layout_type_to_string(LayoutType layout);

// Main detection function: looks up in the arcade database or applies intelligent heuristics
GameProfile detect_game_profile(const std::string& rom_path_or_name);

// Helper to generate layout buttons given a layout type
std::vector<VirtualButtonDesc> generate_layout_buttons(LayoutType layout);

} // namespace arcade

#endif // GAME_PROFILES_H__
