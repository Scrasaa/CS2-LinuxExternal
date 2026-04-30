//
// Created by scrasa on 06.03.26.
//

#ifndef CS2_LINUXEXTERNAL_CONFIG_H
#define CS2_LINUXEXTERNAL_CONFIG_H
#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "SDK/Helper/CEntityCache.h"

struct FlagMeta
{
    EPlayerFlags flag;
    std::string_view json_key;
    std::string_view label;
};

inline constexpr std::array<FlagMeta, 7> k_flag_meta
{{
    { FLAG_DEFUSING,    "defusing",    "Defusing"    },
    { FLAG_PLANTING,    "planting",    "Planting"    },
    { FLAG_SCOPED,      "scoped",      "Scoped"      },
    { FLAG_FLASHED,     "flashed",     "Flashed"     },
    { FLAG_HAS_C4,      "has_c4",      "Has C4"      },
    { FLAG_HAS_DEFUSER, "has_defuser", "Has Defuser" },
    { FLAG_HAS_HELMET,  "has_helmet",  "Has Helmet"  },
}};

namespace cfg
{
    struct Aimbot
    {
        bool bEnable = false;

        bool bVisible = false;

        float fRadius = 0.f;

        float fSmoothness = 0.f;

        bool bAutoShoot = false;
    };

    struct RCS
    {
        bool bEnable = false;
        float f_scale_x = 0.f;
        float f_scale_y = 0.f;
    };

    struct Triggerbot
    {
        bool bEnable = false;

        int iMinReaction = 0;
        int iMaxReaction = 0;
    };

    struct ESP
    {
        struct Player
        {
            bool bEnable = false;
            bool bName = false;
            bool bVisible = false;
            bool bWeapon = false;
            bool bAmmo = false;
            bool bArmor = false;
            bool bHealth = false;
            bool bTeam = false;
            bool bDraw2DBox = false;
            bool bSkeleton = false;
            bool bActiveWeapon = false;
            bool bActiveWeaponIcon = false;

            unsigned int uShowFlags = 0;
        };
        Player player;

        ImColor boxColorEnemy = ImColor(255, 255, 255, 255);
        ImColor skeletonColor = ImColor(255, 255, 255, 255);
        ImColor nameColor = ImColor(255, 255, 255, 255);
        ImColor snaplineColor = ImColor(255, 255, 255, 255);
        ImColor flashColor = ImColor(255, 255,   0, 255);
        ImColor hkColor = ImColor(100, 200, 255, 255);
        ImColor kevlarColor = ImColor(100, 200, 255, 255);
        ImColor scopedColor= ImColor(255, 220,  80, 255);
        ImColor weaponNameColor = ImColor(255, 255, 255, 255);
        ImColor defuserColor = ImColor( 80, 220, 255, 255);
        ImColor defusingColor = ImColor(255,  80,  80, 255);
    };

    struct Visuals
    {
        bool bDrawSnapLines = false;
        bool bDrawFovCircle = false;
    };


    struct Config
    {
        Aimbot aimbot;
        RCS rcs;
        Triggerbot triggerbot;
        ESP esp;
        Visuals visuals;
    };

    std::vector<std::string> GetConfigFiles();
    void Load(const std::string& cfgName);
    void Save(const std::string& cfgName);
    void Delete(const std::string& cfgName);
    void SetDefault(const std::string& cfgName);
    std::string GetDefault();
    bool LoadDefault();
}

inline cfg::Config g_config;

#endif //CS2_LINUXEXTERNAL_CONFIG_H