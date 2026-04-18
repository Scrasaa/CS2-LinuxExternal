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
    EPlayerFlags    flag;
    std::string_view label;
    std::string_view json_key;
};

inline constexpr std::array<FlagMeta, 7> k_flag_meta
{{
    { FLAG_DEFUSING,    "Defusing",     "defusing"    },
    { FLAG_PLANTING,    "Planting",     "planting"    },
    { FLAG_SCOPED,      "Scoped",       "scoped"      },
    { FLAG_FLASHED,     "Flashed",      "flashed"     },
    { FLAG_HAS_C4,      "Has C4",       "has_c4"      },
    { FLAG_HAS_DEFUSER, "Has Defuser",  "has_defuser" },
    { FLAG_HAS_HELMET,  "Has Helmet",   "has_helmet"  },
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

            unsigned int uShowFlags = 0;
        };
        Player player;

        ImColor boxColorEnemy = ImColor(255, 255, 255, 255);
        ImColor skeletonColor = ImColor(255, 255, 255, 255);
    };

    struct Visuals
    {
        bool bDrawSnapLines = false;
        bool bDrawFovCircle = false;
    };


    struct Config
    {
        Aimbot aimbot;
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