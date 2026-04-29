// ── WeaponType.h ──────────────────────────────────────────────

#pragma once
#include <cstdint>
#include <string_view>

// ── Item definition index → weapon type ──────────────────────
//
//  Source: CS2 CEconItemView::m_iItemDefinitionIndex (uint16)
//  Matches avitran0/deadlocked weapon.rs from_index()

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

static const std::unordered_map<uint16_t, std::string> weapon_icon_table =
{
    { 1,  icon(0xE001) }, // Deagle
    { 2,  icon(0xE002) }, // Dual Berettas
    { 3,  icon(0xE003) }, // Five-SeveN
    { 4,  icon(0xE004) }, // Glock

    { 7,  icon(0xE007) }, // AK-47
    { 8,  icon(0xE008) }, // AUG
    { 9,  icon(0xE009) }, // AWP
    { 10, icon(0xE00A) }, // FAMAS
    { 11, icon(0xE00B) }, // G3SG1
    { 13, icon(0xE00D) }, // Galil AR
    { 14, icon(0xE03C) }, // M249
    { 16, icon(0xE00E) }, // M4A4
    { 17, icon(0xE011) }, // MAC-10
    { 19, icon(0xE024) }, // P90
    { 23, "mp5sd"      }, // no glyph
    { 24, icon(0xE018) }, // UMP-45
    { 25, icon(0xE019) }, // XM1014
    { 26, icon(0xE01A) }, // Bizon
    { 27, icon(0xE01B) }, // MAG-7
    { 28, icon(0xE01C) }, // Negev
    { 29, icon(0xE01D) }, // Sawed-Off
    { 30, icon(0xE01E) }, // Tec-9
    { 31, icon(0xE01F) }, // Zeus
    { 32, icon(0xE013) }, // P2000
    { 33, icon(0xE021) }, // MP7
    { 34, icon(0xE022) }, // MP9
    { 35, icon(0xE023) }, // Nova
    { 36, icon(0xE020) }, // P250

    { 38, icon(0xE026) }, // SCAR-20
    { 39, icon(0xE027) }, // SG553
    { 40, icon(0xE028) }, // SSG08

    { 43, icon(0xE02B) }, // Flashbang
    { 44, icon(0xE02C) }, // HE
    { 45, icon(0xE02D) }, // Smoke
    { 46, icon(0xE02E) }, // Molotov
    { 47, icon(0xE02F) }, // Decoy
    { 48, icon(0xE030) }, // Incendiary
    { 49, icon(0xE031) }, // C4

    { 60, icon(0xE010) }, // M4A1-S
    { 61, icon(0xE03D) }, // USP-S
    { 63, icon(0xE03F) }, // CZ75
    { 64, icon(0xE040) }, // Revolver

};

[[nodiscard]] constexpr bool is_knife_id(uint16_t id) noexcept
{
    switch (id)
    {
        case 41:
        case 42:
        case 59:
        case 80:
        case 500: case 505: case 506: case 507: case 508: case 509:
        case 512: case 514: case 515: case 516: case 519: case 520:
        case 522: case 523:
            return true;
        default:
            return false;
    }
}

inline const std::string& get_weapon_icon(uint16_t def_index)
{
    static const std::string knife_icon = icon(0xE02A);
    static const std::string unknown = "?";

    if (is_knife_id(def_index))
        return knife_icon;

    const auto it = weapon_icon_table.find(def_index);
    return it != weapon_icon_table.end() ? it->second : unknown;
}

enum class Eweapon_type : uint8_t
{
    Unknown = 0,

    // Pistols
    Deagle, DualBerettas, FiveSeven, Glock,
    P2000, P250, Revolver, Tec9, Usp, Cz75A,

    // SMGs
    Bizon, Mac10, Mp5Sd, Mp7, Mp9, P90, Ump45,

    // LMGs
    M249, Negev,

    // Shotguns
    Mag7, Nova, Sawedoff, Xm1014,

    // Rifles
    Ak47, Aug, Famas, Galilar, M4A4, M4A1, Sg556,

    // Snipers
    Awp, G3SG1, Scar20, Ssg08,

    // Utility
    Taser,

    // Grenades
    Flashbang, HeGrenade, Smoke, Molotov, Decoy, Incendiary,

    // Equipment
    C4,
    Knife,
};

[[nodiscard]] constexpr Eweapon_type weapon_type_from_index(uint16_t a_index) noexcept
{
    switch (a_index)
    {
        case 1:   return Eweapon_type::Deagle;
        case 2:   return Eweapon_type::DualBerettas;
        case 3:   return Eweapon_type::FiveSeven;
        case 4:   return Eweapon_type::Glock;
        case 7:   return Eweapon_type::Ak47;
        case 8:   return Eweapon_type::Aug;
        case 9:   return Eweapon_type::Awp;
        case 10:  return Eweapon_type::Famas;
        case 11:  return Eweapon_type::G3SG1;
        case 13:  return Eweapon_type::Galilar;
        case 14:  return Eweapon_type::M249;
        case 16:  return Eweapon_type::M4A4;
        case 17:  return Eweapon_type::Mac10;
        case 19:  return Eweapon_type::P90;
        case 23:  return Eweapon_type::Mp5Sd;
        case 24:  return Eweapon_type::Ump45;
        case 25:  return Eweapon_type::Xm1014;
        case 26:  return Eweapon_type::Bizon;
        case 27:  return Eweapon_type::Mag7;
        case 28:  return Eweapon_type::Negev;
        case 29:  return Eweapon_type::Sawedoff;
        case 30:  return Eweapon_type::Tec9;
        case 31:  return Eweapon_type::Taser;
        case 32:  return Eweapon_type::P2000;
        case 33:  return Eweapon_type::Mp7;
        case 34:  return Eweapon_type::Mp9;
        case 35:  return Eweapon_type::Nova;
        case 36:  return Eweapon_type::P250;
        case 38:  return Eweapon_type::Scar20;
        case 39:  return Eweapon_type::Sg556;
        case 40:  return Eweapon_type::Ssg08;
        case 41:
        case 42:
        case 59:
        case 80:
        case 500: case 505: case 506: case 507: case 508: case 509:
        case 512: case 514: case 515: case 516: case 519: case 520:
        case 522: case 523: return Eweapon_type::Knife;
        case 43:  return Eweapon_type::Flashbang;
        case 44:  return Eweapon_type::HeGrenade;
        case 45:  return Eweapon_type::Smoke;
        case 46:  return Eweapon_type::Molotov;
        case 47:  return Eweapon_type::Decoy;
        case 48:  return Eweapon_type::Incendiary;
        case 49:  return Eweapon_type::C4;
        case 60:  return Eweapon_type::M4A1;
        case 61:  return Eweapon_type::Usp;
        case 63:  return Eweapon_type::Cz75A;
        case 64:  return Eweapon_type::Revolver;
        default:  return Eweapon_type::Unknown;
    }
}

[[nodiscard]] constexpr std::string_view weapon_display_name(Eweapon_type a_type) noexcept
{
    switch (a_type)
    {
        case Eweapon_type::Deagle:       return "Desert Eagle";
        case Eweapon_type::DualBerettas: return "Dual Berettas";
        case Eweapon_type::FiveSeven:    return "Five-SeveN";
        case Eweapon_type::Glock:        return "Glock-18";
        case Eweapon_type::P2000:        return "P2000";
        case Eweapon_type::P250:         return "P250";
        case Eweapon_type::Revolver:     return "R8 Revolver";
        case Eweapon_type::Tec9:         return "Tec-9";
        case Eweapon_type::Usp:          return "USP-S";
        case Eweapon_type::Cz75A:        return "CZ75-Auto";
        case Eweapon_type::Bizon:        return "PP-Bizon";
        case Eweapon_type::Mac10:        return "MAC-10";
        case Eweapon_type::Mp5Sd:        return "MP5-SD";
        case Eweapon_type::Mp7:          return "MP7";
        case Eweapon_type::Mp9:          return "MP9";
        case Eweapon_type::P90:          return "P90";
        case Eweapon_type::Ump45:        return "UMP-45";
        case Eweapon_type::M249:         return "M249";
        case Eweapon_type::Negev:        return "Negev";
        case Eweapon_type::Mag7:         return "MAG-7";
        case Eweapon_type::Nova:         return "Nova";
        case Eweapon_type::Sawedoff:     return "Sawed-Off";
        case Eweapon_type::Xm1014:       return "XM1014";
        case Eweapon_type::Ak47:         return "AK-47";
        case Eweapon_type::Aug:          return "AUG";
        case Eweapon_type::Famas:        return "FAMAS";
        case Eweapon_type::Galilar:      return "Galil AR";
        case Eweapon_type::M4A4:         return "M4A4";
        case Eweapon_type::M4A1:         return "M4A1-S";
        case Eweapon_type::Sg556:        return "SG 553";
        case Eweapon_type::Awp:          return "AWP";
        case Eweapon_type::G3SG1:        return "G3SG1";
        case Eweapon_type::Scar20:       return "SCAR-20";
        case Eweapon_type::Ssg08:        return "SSG 08";
        case Eweapon_type::Taser:        return "Zeus x27";
        case Eweapon_type::Flashbang:    return "Flashbang";
        case Eweapon_type::HeGrenade:    return "HE Grenade";
        case Eweapon_type::Smoke:        return "Smoke Grenade";
        case Eweapon_type::Molotov:      return "Molotov Cocktail";
        case Eweapon_type::Decoy:        return "Decoy Grenade";
        case Eweapon_type::Incendiary:   return "Incendiary Grenade";
        case Eweapon_type::C4:           return "C4 Explosive";
        case Eweapon_type::Knife:        return "Knife";
        default:                        return "Unknown";
    }
}

// ── Weapon category helpers ───────────────────────────────────

[[nodiscard]] constexpr bool is_grenade(Eweapon_type a_type) noexcept
{
    switch (a_type)
    {
        case Eweapon_type::Flashbang:
        case Eweapon_type::HeGrenade:
        case Eweapon_type::Smoke:
        case Eweapon_type::Molotov:
        case Eweapon_type::Decoy:
        case Eweapon_type::Incendiary: return true;
        default:                      return false;
    }
}

[[nodiscard]] constexpr bool is_sniper(Eweapon_type a_type) noexcept
{
    switch (a_type)
    {
        case Eweapon_type::Awp:
        case Eweapon_type::G3SG1:
        case Eweapon_type::Scar20:
        case Eweapon_type::Ssg08: return true;
        default:                 return false;
    }
}