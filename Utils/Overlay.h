#pragma once
#include <map>
#include <unordered_map>

#include "imgui.h"

// ─────────────────────────────────────────────────────────────────────────────
// Overlay
// Full-screen transparent X11/GLX/ImGui overlay with XInput2 global hotkey.
//
// Typical usage in main():
//
//   Overlay::Start();   // blocks until g_running == false (SIGINT / window close)
//
// If you need to supply explicit geometry instead of auto-detecting the
// primary monitor, call:
//
//   Overlay::Start(x, y, w, h);
// ─────────────────────────────────────────────────────────────────────────────
// Encodes a Unicode codepoint to a UTF-8 std::string.
// All private-use glyphs (U+E000–U+F8FF) produce exactly 3 bytes.
inline std::string utf8_encode(uint32_t cp) noexcept
{
    if (cp < 0x80u)
    {
        return { static_cast<char>(cp) };
    }
    if (cp < 0x800u)
    {
        return
        {
            static_cast<char>(0xC0u | (cp >> 6u)),
            static_cast<char>(0x80u | (cp & 0x3Fu))
        };
    }
    if (cp < 0x10000u)
    {
        return
        {
            static_cast<char>(0xE0u | (cp >> 12u)),
            static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)),
            static_cast<char>(0x80u | (cp & 0x3Fu))
        };
    }
    return
    {
        static_cast<char>(0xF0u | (cp >> 18u)),
        static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)),
        static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)),
        static_cast<char>(0x80u | (cp & 0x3Fu))
    };
}

// Shorthand — avoids magic hex literals inside the table.
inline std::string icon(const uint32_t cp) noexcept { return utf8_encode(cp); }

static const std::unordered_map<std::string, std::string> weapon_icon_table =
{
    { "deagle",             icon(0xE001) },
    { "elite",              icon(0xE002) },
    { "fiveseven",          icon(0xE003) },
    { "glock",              icon(0xE004) },
    { "ak47",               icon(0xE007) },
    { "aug",                icon(0xE008) },
    { "awp",                icon(0xE009) },
    { "famas",              icon(0xE00A) },
    { "g3sg1",              icon(0xE00B) },
    { "galilar",            icon(0xE00D) },
    { "m249",               icon(0xE03C) },
    { "m4a1",               icon(0xE00E) },
    { "mac10",              icon(0xE011) },
    { "p90",                icon(0xE024) },
    { "mp5sd",              "mp5sd"      },  // no glyph yet
    { "ump45",              icon(0xE018) },
    { "xm1014",             icon(0xE019) },
    { "bizon",              icon(0xE01A) },
    { "mag7",               icon(0xE01B) },
    { "negev",              icon(0xE01C) },
    { "sawedoff",           icon(0xE01D) },
    { "tec9",               icon(0xE01E) },
    { "taser",              icon(0xE01F) },
    { "hkp2000",            icon(0xE013) },
    { "mp7",                icon(0xE021) },
    { "mp9",                icon(0xE022) },
    { "nova",               icon(0xE023) },
    { "p250",               icon(0xE020) },
    { "shield",             "shield"     },
    { "scar20",             icon(0xE026) },
    { "sg556",              icon(0xE027) },
    { "ssg08",              icon(0xE028) },
    { "knife_gg",           "knife_gg"   },
    { "knife",              icon(0xE02A) },
    { "flashbang",          icon(0xE02B) },
    { "hegrenade",          icon(0xE02C) },
    { "smokegrenade",       icon(0xE02D) },
    { "molotov",            icon(0xE02E) },
    { "decoy",              icon(0xE02F) },
    { "incgrenade",         icon(0xE030) },
    { "c4",                 icon(0xE031) },
    { "knife_t",            icon(0xE03B) },
    { "m4a1_silencer",      icon(0xE010) },
    { "usp_silencer",       icon(0xE03D) },
    { "cz75a",              icon(0xE03F) },
    { "revolver",           icon(0xE040) },
    { "knife_bayonet",      icon(0xE1F4) },
    { "knife_css",          icon(0xE02A) },
    { "knife_flip",         icon(0xE1F9) },
    { "knife_gut",          icon(0xE1FA) },
    { "knife_karambit",     icon(0xE1FB) },
    { "knife_m9_bayonet",   icon(0xE1FC) },
    { "knife_tactical",     icon(0xE1FD) },
    { "knife_falchion",     icon(0xE200) },
    { "knife_survival_bowie", icon(0xE202) },
    { "knife_butterfly",    icon(0xE203) },
    { "knife_push",         icon(0xE204) },
    { "knife_cord",         icon(0xE02A) },
    { "knife_canis",        icon(0xE02A) },
    { "knife_ursus",        icon(0xE02A) },
    { "knife_gypsy_jackknife", icon(0xE02A) },
    { "knife_outdoor",      icon(0xE02A) },
    { "knife_stiletto",     icon(0xE02A) },
    { "knife_widowmaker",   icon(0xE02A) },
    { "knife_skeleton",     icon(0xE02A) },
};

inline const std::string& get_weapon_icon(const std::string& weapon_name)
{
    const auto it = weapon_icon_table.find(weapon_name);
    return it != weapon_icon_table.cend() ? it->second : weapon_name;
}

namespace Overlay
{
    // Detects primary monitor geometry, then calls Init → Run → Shutdown.
    // Blocks until the render loop exits. Returns false if Init failed.
    bool Start();

    inline ImFont* small_font{nullptr};
    inline ImFont* weapon_font{nullptr};
	inline ImDrawList* draw_list = nullptr;

    // Explicit geometry variant — use when you already know the target
    // window / monitor rect.
    bool Start(int x, int y, int w, int h);
}