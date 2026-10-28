#include "game_profiles.h"
#include <algorithm>
#include <unordered_map>
#include <cctype>

namespace arcade {

std::string extract_rom_id(const std::string& rom_path_or_name) {
    if (rom_path_or_name.empty()) {
        return "";
    }

    // Strip directory path
    size_t last_slash = rom_path_or_name.find_last_of("/\\");
    std::string filename = (last_slash == std::string::npos) 
                         ? rom_path_or_name 
                         : rom_path_or_name.substr(last_slash + 1);

    // Strip .zip, .7z, .cue, .bin, etc. extension
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string::npos) {
        filename = filename.substr(0, last_dot);
    }

    // Convert to lowercase
    std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    return filename;
}

const char* system_type_to_string(SystemType system) {
    switch (system) {
        case SystemType::SYSTEM_NEOGEO:         return "SNK Neo Geo MVS";
        case SystemType::SYSTEM_CPS1:           return "Capcom CPS-1";
        case SystemType::SYSTEM_CPS2:           return "Capcom CPS-2";
        case SystemType::SYSTEM_CPS3:           return "Capcom CPS-3";
        case SystemType::SYSTEM_PGM:            return "IGS PolyGame Master (PGM)";
        case SystemType::SYSTEM_KONAMI:         return "Konami Arcade";
        case SystemType::SYSTEM_MIDWAY:         return "Midway Arcade";
        case SystemType::SYSTEM_ARCADE_GENERIC: return "Arcade Generic";
        case SystemType::SYSTEM_GAMEBOY:        return "Nintendo Game Boy";
        case SystemType::SYSTEM_GBC:            return "Nintendo Game Boy Color";
        case SystemType::SYSTEM_GBA:            return "Nintendo Game Boy Advance";
    }
    return "Unknown System";
}

const char* layout_type_to_string(LayoutType layout) {
    switch (layout) {
        case LayoutType::LAYOUT_NEO_3BUTTON:      return "Neo Geo 3-Button (Metal Slug)";
        case LayoutType::LAYOUT_NEO_4BUTTON:      return "Neo Geo 4-Button Arc (Fighting)";
        case LayoutType::LAYOUT_CAPCOM_6BUTTON:   return "Capcom 6-Button Grid";
        case LayoutType::LAYOUT_BEATEMUP_2BUTTON: return "Beat-em-up 2-Button";
        case LayoutType::LAYOUT_ARCADE_4BUTTON:    return "Arcade 4-Button";
        case LayoutType::LAYOUT_GAMEBOY:          return "Game Boy 2-Button";
        case LayoutType::LAYOUT_GBA:              return "Game Boy Advance";
    }
    return "Default Layout";
}

std::vector<VirtualButtonDesc> generate_layout_buttons(LayoutType layout) {
    std::vector<VirtualButtonDesc> buttons;

    switch (layout) {
        case LayoutType::LAYOUT_NEO_3BUTTON: {
            // Tailored for Metal Slug:
            // [A] Shoot (Bottom Left), [B] Jump (Bottom Right), [C] Bomb / Grenade (Top Center)
            buttons.push_back({
                "SHOOT", "A", RETRO_DEVICE_ID_JOYPAD_B, 
                -0.52f, 0.35f, 1.20f, 0xEE3333EE // Red
            });
            buttons.push_back({
                "JUMP", "B", RETRO_DEVICE_ID_JOYPAD_A, 
                0.48f, 0.40f, 1.20f, 0x33CC33EE // Green
            });
            buttons.push_back({
                "BOMB", "C", RETRO_DEVICE_ID_JOYPAD_Y, 
                -0.05f, -0.45f, 1.10f, 0x3388FFEE // Blue
            });
            break;
        }

        case LayoutType::LAYOUT_NEO_4BUTTON: {
            // Classic SNK Neo Geo MVS 4-button arch (A-B-C-D):
            // Ergonomically curved for natural thumb sweep with clean spacing
            buttons.push_back({
                "A", "LP", RETRO_DEVICE_ID_JOYPAD_B,
                -0.75f, 0.38f, 0.95f, 0xEE3333EE // Red: Light Punch
            });
            buttons.push_back({
                "B", "LK", RETRO_DEVICE_ID_JOYPAD_A,
                -0.25f, -0.05f, 0.95f, 0xEECC22EE // Yellow: Light Kick
            });
            buttons.push_back({
                "C", "HP", RETRO_DEVICE_ID_JOYPAD_Y,
                0.25f, -0.28f, 0.95f, 0x33CC33EE // Green: Heavy Punch
            });
            buttons.push_back({
                "D", "HK", RETRO_DEVICE_ID_JOYPAD_X,
                0.75f, -0.15f, 0.95f, 0x3388FFEE // Blue: Heavy Kick
            });
            break;
        }

        case LayoutType::LAYOUT_CAPCOM_6BUTTON: {
            // Capcom 6-button fighting layout (SF2, Darkstalkers):
            // Top Row: Light Punch (LP), Medium Punch (MP), Heavy Punch (HP)
            // Bottom Row: Light Kick (LK), Medium Kick (MK), Heavy Kick (HK)
            // Sized with 0.82f radius scale to guarantee zero collision across all phone aspect ratios
            buttons.push_back({
                "LP", "Y", RETRO_DEVICE_ID_JOYPAD_Y,
                -0.62f, -0.45f, 0.82f, 0x3399FFEE // Light Blue
            });
            buttons.push_back({
                "MP", "X", RETRO_DEVICE_ID_JOYPAD_X,
                0.00f, -0.50f, 0.82f, 0xEECC22EE // Yellow
            });
            buttons.push_back({
                "HP", "L", RETRO_DEVICE_ID_JOYPAD_L,
                0.62f, -0.45f, 0.82f, 0xEE3333EE // Red
            });

            buttons.push_back({
                "LK", "B", RETRO_DEVICE_ID_JOYPAD_B,
                -0.62f, 0.45f, 0.82f, 0x3399FFEE // Light Blue
            });
            buttons.push_back({
                "MK", "A", RETRO_DEVICE_ID_JOYPAD_A,
                0.00f, 0.50f, 0.82f, 0xEECC22EE // Yellow
            });
            buttons.push_back({
                "HK", "R", RETRO_DEVICE_ID_JOYPAD_R,
                0.62f, 0.45f, 0.82f, 0xEE3333EE // Red
            });
            break;
        }

        case LayoutType::LAYOUT_BEATEMUP_2BUTTON: {
            // Beat-em-ups (Cadillacs & Dinosaurs, Punisher, Final Fight):
            // Huge Attack and Jump buttons
            buttons.push_back({
                "ATTACK", "1", RETRO_DEVICE_ID_JOYPAD_B,
                -0.45f, 0.15f, 1.30f, 0xEE3333EE // Large Red Attack
            });
            buttons.push_back({
                "JUMP", "2", RETRO_DEVICE_ID_JOYPAD_A,
                0.45f, -0.15f, 1.30f, 0x3388FFEE // Large Blue Jump
            });
            break;
        }

        case LayoutType::LAYOUT_GAMEBOY: {
            // Nintendo Game Boy / GBC 2-Button layout (classic diagonal B and A)
            buttons.push_back({
                "B", "B", RETRO_DEVICE_ID_JOYPAD_B,
                -0.38f, 0.18f, 1.15f, 0x992255EE
            });
            buttons.push_back({
                "A", "A", RETRO_DEVICE_ID_JOYPAD_A,
                0.38f, -0.18f, 1.15f, 0x992255EE
            });
            break;
        }

        case LayoutType::LAYOUT_GBA: {
            // Game Boy Advance layout (B, A + L/R shoulder triggers)
            buttons.push_back({
                "B", "B", RETRO_DEVICE_ID_JOYPAD_B,
                -0.38f, 0.18f, 1.12f, 0x444488EE
            });
            buttons.push_back({
                "A", "A", RETRO_DEVICE_ID_JOYPAD_A,
                0.38f, -0.18f, 1.12f, 0x444488EE
            });
            buttons.push_back({
                "L", "L", RETRO_DEVICE_ID_JOYPAD_L,
                -10.0f, 0.0f, 1.05f, 0x556677EE
            });
            buttons.push_back({
                "R", "R", RETRO_DEVICE_ID_JOYPAD_R,
                -9.0f, 0.0f, 1.05f, 0x556677EE
            });
            break;
        }

        case LayoutType::LAYOUT_ARCADE_4BUTTON:
        default: {
            // Diamond arcade layout
            buttons.push_back({
                "A", "1", RETRO_DEVICE_ID_JOYPAD_B,
                0.00f, 0.55f, 1.10f, 0x33CC33EE // Bottom (Green)
            });
            buttons.push_back({
                "B", "2", RETRO_DEVICE_ID_JOYPAD_A,
                0.55f, 0.00f, 1.10f, 0xEE3333EE // Right (Red)
            });
            buttons.push_back({
                "C", "3", RETRO_DEVICE_ID_JOYPAD_Y,
                -0.55f, 0.00f, 1.10f, 0x3388FFEE // Left (Blue)
            });
            buttons.push_back({
                "D", "4", RETRO_DEVICE_ID_JOYPAD_X,
                0.00f, -0.55f, 1.10f, 0xEECC22EE // Top (Yellow)
            });
            break;
        }
    }

    return buttons;
}

// Static database of known arcade titles
static const std::unordered_map<std::string, GameProfile> s_game_database = {
    // Metal Slug Series (Neo Geo - 3-button layout)
    {"mslug",    {"mslug",    "Metal Slug - Super Vehicle-001", "SNK / Nazca", 1996, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"mslug2",   {"mslug2",   "Metal Slug 2 - Super Vehicle-001/II", "SNK", 1998, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"mslugx",   {"mslugx",   "Metal Slug X - Super Vehicle-001", "SNK", 1999, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"mslug3",   {"mslug3",   "Metal Slug 3", "SNK", 2000, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"mslug4",   {"mslug4",   "Metal Slug 4", "Mega / Playmore", 2002, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"mslug5",   {"mslug5",   "Metal Slug 5", "SNK Playmore", 2003, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},

    // Neo Geo Fighting Games (4-button curved layout)
    {"garou",    {"garou",    "Garou: Mark of the Wolves", "SNK", 1999, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof94",    {"kof94",    "The King of Fighters '94", "SNK", 1994, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof95",    {"kof95",    "The King of Fighters '95", "SNK", 1995, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof96",    {"kof96",    "The King of Fighters '96", "SNK", 1996, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof97",    {"kof97",    "The King of Fighters '97", "SNK", 1997, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof98",    {"kof98",    "The King of Fighters '98 - The Slugfest", "SNK", 1998, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof99",    {"kof99",    "The King of Fighters '99 - Millennium Battle", "SNK", 1999, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof99hp",  {"kof99hp",  "The King of Fighters '99 (Hack)", "SNK", 1999, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof2000",  {"kof2000",  "The King of Fighters 2000", "SNK", 2000, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof2001",  {"kof2001",  "The King of Fighters 2001", "Eolith / SNK", 2001, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof2002",  {"kof2002",  "The King of Fighters 2002 - Challenge to Ultimate Battle", "Eolith / Playmore", 2002, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"kof2003",  {"kof2003",  "The King of Fighters 2003", "SNK Playmore", 2003, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"lastbld",  {"lastbld",  "The Last Blade", "SNK", 1997, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"lastbld2", {"lastbld2", "The Last Blade 2", "SNK", 1998, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"rbff1",    {"rbff1",    "Real Bout Fatal Fury", "SNK", 1995, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"rbff2",    {"rbff2",    "Real Bout Fatal Fury 2 - The Newcomers", "SNK", 1998, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"rbffspec", {"rbffspec", "Real Bout Fatal Fury Special", "SNK", 1997, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"samsho3",  {"samsho3",  "Samurai Shodown III - Blades of Blood", "SNK", 1995, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"samsho4",  {"samsho4",  "Samurai Shodown IV - Amakusa's Revenge", "SNK", 1996, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"samsho5",  {"samsho5",  "Samurai Shodown V", "Yuki Enterprise / SNK Playmore", 2003, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"sengoku3", {"sengoku3", "Sengoku 3 / Sengoku Denshou 2001", "Noise Factory / SNK", 2001, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},
    {"wjammers", {"wjammers", "Windjammers / Flying Power Disc", "Data East", 1994, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_4BUTTON, 1, {}}},

    // Capcom 6-Button Fighting Games
    {"sf2",      {"sf2",      "Street Fighter II - The World Warrior", "Capcom", 1991, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"sf2ce",    {"sf2ce",    "Street Fighter II' - Champion Edition", "Capcom", 1992, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"sf2hf",    {"sf2hf",    "Street Fighter II' - Hyper Fighting", "Capcom", 1992, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"ssf2",     {"ssf2",     "Super Street Fighter II - The New Challengers", "Capcom", 1993, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"ssf2t",    {"ssf2t",    "Super Street Fighter II Turbo", "Capcom", 1994, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"hsf2",     {"hsf2",     "Hyper Street Fighter II - The Anniversary Edition", "Capcom", 2003, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"sfa",      {"sfa",      "Street Fighter Alpha - Warriors' Dreams", "Capcom", 1995, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"sfa2",     {"sfa2",     "Street Fighter Alpha 2", "Capcom", 1996, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"sfa3",     {"sfa3",     "Street Fighter Alpha 3", "Capcom", 1998, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"msh",      {"msh",      "Marvel Super Heroes", "Capcom", 1995, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"mshvsf",   {"mshvsf",   "Marvel Super Heroes vs Street Fighter", "Capcom", 1997, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"mvsc",     {"mvsc",     "Marvel vs. Capcom - Clash of Super Heroes", "Capcom", 1998, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"vhunt2",   {"vhunt2",   "Vampire Hunter 2 - Darkstalkers Revenge", "Capcom", 1997, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"vsav",     {"vsav",     "Vampire Savior - The Lord of Vampire", "Capcom", 1997, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},
    {"vsav2",    {"vsav2",    "Vampire Savior 2 - The Lord of Vampire", "Capcom", 1997, SystemType::SYSTEM_CPS2, LayoutType::LAYOUT_CAPCOM_6BUTTON, 1, {}}},

    // Beat-em-ups (2-button layout)
    {"dino",     {"dino",     "Cadillacs and Dinosaurs", "Capcom", 1993, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"punisher", {"punisher", "The Punisher", "Capcom", 1993, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"ffight",   {"ffight",   "Final Fight", "Capcom", 1989, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"wof",      {"wof",      "Warriors of Fate", "Capcom", 1992, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"captcomm", {"captcomm", "Captain Commando", "Capcom", 1991, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"knights",  {"knights",  "Knights of the Round", "Capcom", 1991, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},

    // IGS PGM System
    {"martmast", {"martmast", "Martial Masters", "IGS", 2001, SystemType::SYSTEM_PGM, LayoutType::LAYOUT_ARCADE_4BUTTON, 1, {}}},
    {"killbld",  {"killbld",  "The Killing Blade", "IGS", 1998, SystemType::SYSTEM_PGM, LayoutType::LAYOUT_ARCADE_4BUTTON, 1, {}}},

    // Classic / Shooters / Others
    {"1942",     {"1942",     "1942", "Capcom", 1984, SystemType::SYSTEM_CPS1, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"nam1975",  {"nam1975",  "NAM-1975", "SNK", 1990, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"spinmast", {"spinmast", "Spin Master / Miracle Adventure", "Data East", 1993, SystemType::SYSTEM_NEOGEO, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"ssriders", {"ssriders", "Sunset Riders", "Konami", 1991, SystemType::SYSTEM_KONAMI, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"cabal",    {"cabal",    "Cabal", "TAD Corporation", 1988, SystemType::SYSTEM_ARCADE_GENERIC, LayoutType::LAYOUT_NEO_3BUTTON, 1, {}}},
    {"tengai",   {"tengai",   "Tengai / Sengoku Blade", "Psikyo", 1996, SystemType::SYSTEM_ARCADE_GENERIC, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}},
    {"rallyx",   {"rallyx",   "Rally-X", "Namco", 1980, SystemType::SYSTEM_ARCADE_GENERIC, LayoutType::LAYOUT_BEATEMUP_2BUTTON, 1, {}}}
};

GameProfile detect_game_profile(const std::string& rom_path_or_name) {
    std::string id = extract_rom_id(rom_path_or_name);

    // 1. Direct database lookup
    auto it = s_game_database.find(id);
    if (it != s_game_database.end()) {
        GameProfile profile = it->second;
        profile.buttons = generate_layout_buttons(profile.layout);
        return profile;
    }

    // 2. Intelligent System Type / Prefix Heuristics
    GameProfile profile;
    profile.rom_id = id;
    profile.run_ahead_frames = 1;

    std::string lower_full = rom_path_or_name;
    std::transform(lower_full.begin(), lower_full.end(), lower_full.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    std::string ext;
    size_t last_dot = lower_full.find_last_of('.');
    if (last_dot != std::string::npos) {
        ext = lower_full.substr(last_dot);
    }
    if (ext == ".zip" || ext == ".7z") {
        size_t prev_dot = lower_full.substr(0, last_dot).find_last_of('.');
        if (prev_dot != std::string::npos) {
            std::string sub_ext = lower_full.substr(prev_dot, last_dot - prev_dot);
            if (sub_ext == ".gb" || sub_ext == ".gbc" || sub_ext == ".gba") {
                ext = sub_ext;
            }
        }
    }

    if (ext == ".gbc" || lower_full.find("gbc") != std::string::npos || id == "gbc") {
        profile.title = "Game Boy Color (" + id + ")";
        profile.manufacturer = "Nintendo";
        profile.year = 1998;
        profile.system = SystemType::SYSTEM_GBC;
        profile.layout = LayoutType::LAYOUT_GAMEBOY;
        profile.run_ahead_frames = 1;
    } else if (ext == ".gba" || lower_full.find("gba") != std::string::npos || id == "gba") {
        profile.title = "Game Boy Advance (" + id + ")";
        profile.manufacturer = "Nintendo";
        profile.year = 2001;
        profile.system = SystemType::SYSTEM_GBA;
        profile.layout = LayoutType::LAYOUT_GBA;
        profile.run_ahead_frames = 1;
    } else if (ext == ".gb" || lower_full.find("gameboy") != std::string::npos || id == "gameboy" || id == "gb") {
        profile.title = "Game Boy (" + id + ")";
        profile.manufacturer = "Nintendo";
        profile.year = 1989;
        profile.system = SystemType::SYSTEM_GAMEBOY;
        profile.layout = LayoutType::LAYOUT_GAMEBOY;
        profile.run_ahead_frames = 1;
    } else if (id == "neogeo" || id == "mvs" || id == "snk") {
        profile.title = "SNK Neo Geo Arcade";
        profile.manufacturer = "SNK";
        profile.year = 1990;
        profile.system = SystemType::SYSTEM_NEOGEO;
        profile.layout = LayoutType::LAYOUT_NEO_4BUTTON;
    } else if (id == "cps" || id == "cps1" || id == "cps2" || id == "cps3" || id == "capcom") {
        profile.title = "Capcom CPS Arcade";
        profile.manufacturer = "Capcom";
        profile.year = 1995;
        profile.system = (id == "cps1") ? SystemType::SYSTEM_CPS1 :
                         (id == "cps3") ? SystemType::SYSTEM_CPS3 : SystemType::SYSTEM_CPS2;
        profile.layout = LayoutType::LAYOUT_CAPCOM_6BUTTON;
    } else if (id == "beatemup") {
        profile.title = "Capcom Beat 'Em Up";
        profile.manufacturer = "Capcom";
        profile.year = 1992;
        profile.system = SystemType::SYSTEM_CPS1;
        profile.layout = LayoutType::LAYOUT_BEATEMUP_2BUTTON;
    } else if (id == "pgm" || id == "igs") {
        profile.title = "IGS PGM Arcade";
        profile.manufacturer = "IGS";
        profile.year = 2000;
        profile.system = SystemType::SYSTEM_PGM;
        profile.layout = LayoutType::LAYOUT_ARCADE_4BUTTON;
    } else if (id.rfind("mslug", 0) == 0) {
        // Metal Slug variant
        profile.title = "Metal Slug (" + id + ")";
        profile.manufacturer = "SNK";
        profile.year = 1996;
        profile.system = SystemType::SYSTEM_NEOGEO;
        profile.layout = LayoutType::LAYOUT_NEO_3BUTTON;
    } else if (id.rfind("kof", 0) == 0 || id.rfind("garou", 0) == 0 ||
               id.rfind("samsho", 0) == 0 || id.rfind("lastbld", 0) == 0 ||
               id.rfind("rbff", 0) == 0 || id.rfind("sengoku", 0) == 0 ||
               id.rfind("fatfur", 0) == 0 || id.rfind("aof", 0) == 0 ||
               id.rfind("wjammers", 0) == 0) {
        // Neo Geo Fighting game
        profile.title = "Neo Geo Arcade (" + id + ")";
        profile.manufacturer = "SNK";
        profile.year = 1998;
        profile.system = SystemType::SYSTEM_NEOGEO;
        profile.layout = LayoutType::LAYOUT_NEO_4BUTTON;
    } else if (id.rfind("sf", 0) == 0 || id.rfind("ssf", 0) == 0 ||
               id.rfind("hsf", 0) == 0 || id.rfind("sfa", 0) == 0 ||
               id.rfind("msh", 0) == 0 || id.rfind("vsav", 0) == 0 ||
               id.rfind("vhunt", 0) == 0 || id.rfind("dstlk", 0) == 0 ||
               id.find("vs") != std::string::npos) {
        // Capcom 6-button Fighter
        profile.title = "Capcom Fighter (" + id + ")";
        profile.manufacturer = "Capcom";
        profile.year = 1995;
        profile.system = SystemType::SYSTEM_CPS2;
        profile.layout = LayoutType::LAYOUT_CAPCOM_6BUTTON;
    } else if (id.rfind("dino", 0) == 0 || id.rfind("punisher", 0) == 0 ||
               id.rfind("ffight", 0) == 0 || id.rfind("wof", 0) == 0 ||
               id.rfind("captcomm", 0) == 0 || id.rfind("knights", 0) == 0) {
        // Beat-em-up 2-button
        profile.title = "Beat 'Em Up Arcade (" + id + ")";
        profile.manufacturer = "Capcom";
        profile.year = 1992;
        profile.system = SystemType::SYSTEM_CPS1;
        profile.layout = LayoutType::LAYOUT_BEATEMUP_2BUTTON;
    } else if (id.rfind("martmast", 0) == 0 || id.rfind("killbld", 0) == 0 ||
               id.rfind("kov", 0) == 0 || id.rfind("olds", 0) == 0) {
        // PGM
        profile.title = "PGM Arcade (" + id + ")";
        profile.manufacturer = "IGS";
        profile.year = 2000;
        profile.system = SystemType::SYSTEM_PGM;
        profile.layout = LayoutType::LAYOUT_ARCADE_4BUTTON;
    } else {
        // Default generic arcade fallback
        profile.title = "Arcade Game (" + id + ")";
        profile.manufacturer = "Arcade";
        profile.year = 1990;
        profile.system = SystemType::SYSTEM_ARCADE_GENERIC;
        profile.layout = LayoutType::LAYOUT_ARCADE_4BUTTON;
    }

    profile.buttons = generate_layout_buttons(profile.layout);
    return profile;
}

} // namespace arcade
