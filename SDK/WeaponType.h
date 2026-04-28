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

enum class EWeaponType : uint8_t
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

[[nodiscard]] constexpr EWeaponType weapon_type_from_index(uint16_t a_index) noexcept
{
    switch (a_index)
    {
        case 1:   return EWeaponType::Deagle;
        case 2:   return EWeaponType::DualBerettas;
        case 3:   return EWeaponType::FiveSeven;
        case 4:   return EWeaponType::Glock;
        case 7:   return EWeaponType::Ak47;
        case 8:   return EWeaponType::Aug;
        case 9:   return EWeaponType::Awp;
        case 10:  return EWeaponType::Famas;
        case 11:  return EWeaponType::G3SG1;
        case 13:  return EWeaponType::Galilar;
        case 14:  return EWeaponType::M249;
        case 16:  return EWeaponType::M4A4;
        case 17:  return EWeaponType::Mac10;
        case 19:  return EWeaponType::P90;
        case 23:  return EWeaponType::Mp5Sd;
        case 24:  return EWeaponType::Ump45;
        case 25:  return EWeaponType::Xm1014;
        case 26:  return EWeaponType::Bizon;
        case 27:  return EWeaponType::Mag7;
        case 28:  return EWeaponType::Negev;
        case 29:  return EWeaponType::Sawedoff;
        case 30:  return EWeaponType::Tec9;
        case 31:  return EWeaponType::Taser;
        case 32:  return EWeaponType::P2000;
        case 33:  return EWeaponType::Mp7;
        case 34:  return EWeaponType::Mp9;
        case 35:  return EWeaponType::Nova;
        case 36:  return EWeaponType::P250;
        case 38:  return EWeaponType::Scar20;
        case 39:  return EWeaponType::Sg556;
        case 40:  return EWeaponType::Ssg08;
        case 41:
        case 42:
        case 59:
        case 80:
        case 500: case 505: case 506: case 507: case 508: case 509:
        case 512: case 514: case 515: case 516: case 519: case 520:
        case 522: case 523: return EWeaponType::Knife;
        case 43:  return EWeaponType::Flashbang;
        case 44:  return EWeaponType::HeGrenade;
        case 45:  return EWeaponType::Smoke;
        case 46:  return EWeaponType::Molotov;
        case 47:  return EWeaponType::Decoy;
        case 48:  return EWeaponType::Incendiary;
        case 49:  return EWeaponType::C4;
        case 60:  return EWeaponType::M4A1;
        case 61:  return EWeaponType::Usp;
        case 63:  return EWeaponType::Cz75A;
        case 64:  return EWeaponType::Revolver;
        default:  return EWeaponType::Unknown;
    }
}

[[nodiscard]] constexpr std::string_view weapon_display_name(EWeaponType a_type) noexcept
{
    switch (a_type)
    {
        case EWeaponType::Deagle:       return "Desert Eagle";
        case EWeaponType::DualBerettas: return "Dual Berettas";
        case EWeaponType::FiveSeven:    return "Five-SeveN";
        case EWeaponType::Glock:        return "Glock-18";
        case EWeaponType::P2000:        return "P2000";
        case EWeaponType::P250:         return "P250";
        case EWeaponType::Revolver:     return "R8 Revolver";
        case EWeaponType::Tec9:         return "Tec-9";
        case EWeaponType::Usp:          return "USP-S";
        case EWeaponType::Cz75A:        return "CZ75-Auto";
        case EWeaponType::Bizon:        return "PP-Bizon";
        case EWeaponType::Mac10:        return "MAC-10";
        case EWeaponType::Mp5Sd:        return "MP5-SD";
        case EWeaponType::Mp7:          return "MP7";
        case EWeaponType::Mp9:          return "MP9";
        case EWeaponType::P90:          return "P90";
        case EWeaponType::Ump45:        return "UMP-45";
        case EWeaponType::M249:         return "M249";
        case EWeaponType::Negev:        return "Negev";
        case EWeaponType::Mag7:         return "MAG-7";
        case EWeaponType::Nova:         return "Nova";
        case EWeaponType::Sawedoff:     return "Sawed-Off";
        case EWeaponType::Xm1014:       return "XM1014";
        case EWeaponType::Ak47:         return "AK-47";
        case EWeaponType::Aug:          return "AUG";
        case EWeaponType::Famas:        return "FAMAS";
        case EWeaponType::Galilar:      return "Galil AR";
        case EWeaponType::M4A4:         return "M4A4";
        case EWeaponType::M4A1:         return "M4A1-S";
        case EWeaponType::Sg556:        return "SG 553";
        case EWeaponType::Awp:          return "AWP";
        case EWeaponType::G3SG1:        return "G3SG1";
        case EWeaponType::Scar20:       return "SCAR-20";
        case EWeaponType::Ssg08:        return "SSG 08";
        case EWeaponType::Taser:        return "Zeus x27";
        case EWeaponType::Flashbang:    return "Flashbang";
        case EWeaponType::HeGrenade:    return "HE Grenade";
        case EWeaponType::Smoke:        return "Smoke Grenade";
        case EWeaponType::Molotov:      return "Molotov Cocktail";
        case EWeaponType::Decoy:        return "Decoy Grenade";
        case EWeaponType::Incendiary:   return "Incendiary Grenade";
        case EWeaponType::C4:           return "C4 Explosive";
        case EWeaponType::Knife:        return "Knife";
        default:                        return "Unknown";
    }
}

// ── Weapon category helpers ───────────────────────────────────

[[nodiscard]] constexpr bool is_grenade(EWeaponType a_type) noexcept
{
    switch (a_type)
    {
        case EWeaponType::Flashbang:
        case EWeaponType::HeGrenade:
        case EWeaponType::Smoke:
        case EWeaponType::Molotov:
        case EWeaponType::Decoy:
        case EWeaponType::Incendiary: return true;
        default:                      return false;
    }
}

[[nodiscard]] constexpr bool is_sniper(EWeaponType a_type) noexcept
{
    switch (a_type)
    {
        case EWeaponType::Awp:
        case EWeaponType::G3SG1:
        case EWeaponType::Scar20:
        case EWeaponType::Ssg08: return true;
        default:                 return false;
    }
}