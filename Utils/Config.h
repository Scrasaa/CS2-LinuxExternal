//
// Created by scrasa on 06.03.26.
//

#ifndef CS2_LINUXEXTERNAL_CONFIG_H
#define CS2_LINUXEXTERNAL_CONFIG_H
#include <string>
#include <vector>

#include "imgui.h"


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

            bool bIsFlashed = false;
            bool bIsReloading = false;
            bool bIsScoping = false;

            bool bDefusing = false;
            bool bhasDefuser = false;
            bool bhasC4 = false;
            bool bIsPlanting = false;
        };
        Player player;

        ImColor boxColorEnemy = ImColor(255, 255, 255, 255);
        ImColor boxColorTeam = ImColor(255, 255, 255, 255);
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
}

inline cfg::Config g_config;

#endif //CS2_LINUXEXTERNAL_CONFIG_H