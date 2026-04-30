//
// Created by scrasa on 06.03.26.
//

#include <fstream>
#include "Config.h"

#include "../Thirdparty/json.h"

using json = nlohmann::json;

constexpr auto defaultCfgPath  = "/home/scrasa/.config/cs2-external/";
constexpr auto k_default_cfg_path = "/home/scrasa/.config/cs2-external/default.json";

//------------------------ Helper Functions
inline void to_json(nlohmann::json& j, const PlayerInfo& p)
{
    j =
    {
        { "name",       p.szName             },
        { "health",     p.iHealth            },
        { "max_health", p.iMaxHealth         },
        { "armor",      p.iArmor             },
        { "weapon",     p.szActiveWeaponName },
    };

    auto& flags_node = j["flags"];
    for (const auto& meta : k_flag_meta)
        flags_node[meta.json_key.data()] = p.HasFlag(meta.flag);
}

inline void from_json(const nlohmann::json& j, PlayerInfo& p)
{
    j.at("name")       .get_to(p.szName);
    j.at("health")     .get_to(p.iHealth);
    j.at("max_health") .get_to(p.iMaxHealth);
    j.at("armor")      .get_to(p.iArmor);
    j.at("weapon")     .get_to(p.szActiveWeaponName);

    p.flags = 0;

    if (!j.contains("flags"))
        return;

    const auto& flags_node = j.at("flags");
    for (const auto& meta : k_flag_meta)
    {
        if (flags_node.value(meta.json_key.data(), false))
            p.SetFlag(meta.flag);
    }
}


std::vector<std::string> cfg::GetConfigFiles()
{
    std::vector<std::string> files;

    if (!std::filesystem::exists(defaultCfgPath) || !std::filesystem::is_directory(defaultCfgPath))
        return files;

    for (const auto& entry : std::filesystem::directory_iterator(defaultCfgPath))
    {
        if (entry.is_regular_file())
            files.push_back(entry.path().filename().string());
    }
    return files;
}

inline json ImColorToJson(const ImColor& color)
{
    return json::array({ color.Value.x, color.Value.y, color.Value.z, color.Value.w });
}

inline ImColor JsonToImColor(const json& j, const ImColor& defaultColor = ImColor(255, 255, 255, 255))
{
    if (!j.is_array() || j.size() != 4)
        return defaultColor;

    try
    {
        return {
            j[0].get<float>(),
            j[1].get<float>(),
            j[2].get<float>(),
            j[3].get<float>()
        };
    }
    catch (...)
    {
        return defaultColor;
    }
}
//------------------------ Cfg Functions

void cfg::Load(const std::string& cfgName)
{
    std::ifstream file(defaultCfgPath + cfgName);
    if (!file.is_open()) return;

    json j;
    file >> j;

    // ----- Aimbot -----
    auto jAimbot = j.value("Aimbot", json::object());
    g_config.aimbot.bEnable             = jAimbot.value("bEnable", g_config.aimbot.bEnable);
    g_config.aimbot.bVisible            = jAimbot.value("bVisible", g_config.aimbot.bVisible);
    g_config.aimbot.fRadius             = jAimbot.value("fRadius", g_config.aimbot.fRadius);
    g_config.aimbot.fSmoothness         = jAimbot.value("fSmoothness", g_config.aimbot.fSmoothness);
    g_config.aimbot.bAutoShoot          = jAimbot.value("bAutoShoot", g_config.aimbot.bAutoShoot);

    // ----- RCS -----
    auto jRCS = j.value("RCS", json::object());
    g_config.rcs.bEnable             = jRCS.value("bEnable", g_config.rcs.bEnable);
    g_config.rcs.f_scale_x             = jRCS.value("f_scale_x", g_config.rcs.f_scale_x);
    g_config.rcs.f_scale_y             = jRCS.value("f_scale_y", g_config.rcs.f_scale_y);

    // ----- Aimbot -----
    auto jTriggerbot = j.value("Triggerbot", json::object());
    g_config.triggerbot.bEnable             = jTriggerbot.value("bEnable", g_config.triggerbot.bEnable);
    g_config.triggerbot.iMinReaction        = jTriggerbot.value("fMinReaction", g_config.triggerbot.iMinReaction);
    g_config.triggerbot.iMaxReaction        = jTriggerbot.value("fMaxReaction", g_config.triggerbot.iMaxReaction);

    // ----- ESP: Players -----
    auto jPlayerESP = j.value("ESP_Players", json::object());
    g_config.esp.player.bEnable         = jPlayerESP.value("bEnable", g_config.esp.player.bEnable);
    g_config.esp.player.bDraw2DBox      = jPlayerESP.value("bDraw2DBox", g_config.esp.player.bDraw2DBox);
    g_config.esp.player.bName           = jPlayerESP.value("bName", g_config.esp.player.bName);
    g_config.esp.player.bTeam           = jPlayerESP.value("bTeam", g_config.esp.player.bTeam);
    g_config.esp.player.bHealth         = jPlayerESP.value("bHealth", g_config.esp.player.bHealth);
    g_config.esp.player.bSkeleton       = jPlayerESP.value("bSkeleton", g_config.esp.player.bSkeleton);
    g_config.esp.player.bActiveWeapon   = jPlayerESP.value("bActiveWeapon", g_config.esp.player.bActiveWeapon);
    g_config.esp.player.bActiveWeaponIcon   = jPlayerESP.value("bActiveWeaponIcon", g_config.esp.player.bActiveWeaponIcon);

    g_config.esp.boxColorEnemy   = JsonToImColor(jPlayerESP.value("boxColorEnemy", ImColorToJson(g_config.esp.boxColorEnemy)));
    g_config.esp.skeletonColor   = JsonToImColor(jPlayerESP.value("skeletonColor", ImColorToJson(g_config.esp.skeletonColor)));
    g_config.esp.nameColor       = JsonToImColor(jPlayerESP.value("nameColor", ImColorToJson(g_config.esp.nameColor)));
    g_config.esp.snaplineColor   = JsonToImColor(jPlayerESP.value("snaplineColor", ImColorToJson(g_config.esp.snaplineColor)));
    g_config.esp.flashColor      = JsonToImColor(jPlayerESP.value("flashColor", ImColorToJson(g_config.esp.flashColor)));
    g_config.esp.hkColor         = JsonToImColor(jPlayerESP.value("hkColor", ImColorToJson(g_config.esp.hkColor)));
    g_config.esp.kevlarColor         = JsonToImColor(jPlayerESP.value("kevlarColor", ImColorToJson(g_config.esp.kevlarColor)));
    g_config.esp.scopedColor     = JsonToImColor(jPlayerESP.value("scopedColor", ImColorToJson(g_config.esp.scopedColor)));
    g_config.esp.weaponNameColor = JsonToImColor(jPlayerESP.value("weaponNameColor", ImColorToJson(g_config.esp.weaponNameColor)));
    g_config.esp.defuserColor    = JsonToImColor(jPlayerESP.value("defuserColor", ImColorToJson(g_config.esp.defuserColor)));
    g_config.esp.defusingColor   = JsonToImColor(jPlayerESP.value("defusingColor", ImColorToJson(g_config.esp.defusingColor)));

    g_config.esp.player.uShowFlags = 0;
    const auto& jShowFlags = jPlayerESP.value("show_flags", json::object());
    for (const auto& meta : k_flag_meta)
    {
        if (jShowFlags.value(meta.json_key.data(), false))
            g_config.esp.player.uShowFlags |= static_cast<uint8_t>(meta.flag);
    }

    // ----- Visuals -----
    auto jVisuals = j.value("Visuals", json::object());
    g_config.visuals.bDrawFovCircle             = jVisuals.value("bDrawFovCircle", g_config.visuals.bDrawFovCircle);
    g_config.visuals.bDrawSnapLines             = jVisuals.value("bDrawSnapLines", g_config.visuals.bDrawSnapLines);
}

void cfg::Save(const std::string& cfgName)
{
    std::filesystem::create_directories(defaultCfgPath);

    json j;

  // ----- Aimbot -----
    j["Aimbot"] =
    {
        {"bEnable", g_config.aimbot.bEnable},
        {"bVisible", g_config.aimbot.bVisible},
        {"fRadius", g_config.aimbot.fRadius},
        {"fSmoothness", g_config.aimbot.fSmoothness},
        {"bAutoShoot", g_config.aimbot.bAutoShoot},
    };

    // ----- RCS -----
    j["RCS"] =
    {
        {"bEnable", g_config.rcs.bEnable},
        {"f_scale_x", g_config.rcs.f_scale_x},
        {"f_scale_y", g_config.rcs.f_scale_y},
    };

    j["Triggerbot"] =
    {
        {"bEnable", g_config.triggerbot.bEnable},
        {"fMinReaction", g_config.triggerbot.iMinReaction},
        {"fMaxReaction", g_config.triggerbot.iMaxReaction},
    };

    // ----- ESP: Players -----
    j["ESP_Players"] =
    {
        {"bEnable", g_config.esp.player.bEnable},
        {"bSkeleton", g_config.esp.player.bSkeleton},
        {"bName", g_config.esp.player.bName},
        {"bActiveWeapon", g_config.esp.player.bActiveWeapon},
        {"bActiveWeaponIcon", g_config.esp.player.bActiveWeaponIcon},
        {"bAmmo", g_config.esp.player.bAmmo},
        {"bHealth", g_config.esp.player.bHealth},
        {"bDraw2DBox", g_config.esp.player.bDraw2DBox},
        {"bVisible", g_config.esp.player.bVisible},
        {"bTeam", g_config.esp.player.bTeam},
        {"boxColorEnemy",
           {
               g_config.esp.boxColorEnemy.Value.x,
               g_config.esp.boxColorEnemy.Value.y,
               g_config.esp.boxColorEnemy.Value.z,
               g_config.esp.boxColorEnemy.Value.w
           }
        },
        {"skeletonColor",
            {
                g_config.esp.skeletonColor.Value.x,
                g_config.esp.skeletonColor.Value.y,
                g_config.esp.skeletonColor.Value.z,
                g_config.esp.skeletonColor.Value.w
            }
        },
        {"nameColor",
        {
            g_config.esp.nameColor.Value.x,
            g_config.esp.nameColor.Value.y,
            g_config.esp.nameColor.Value.z,
            g_config.esp.nameColor.Value.w
        }
        },
        {"snaplineColor",
            {
                g_config.esp.snaplineColor.Value.x,
                g_config.esp.snaplineColor.Value.y,
                g_config.esp.snaplineColor.Value.z,
                g_config.esp.snaplineColor.Value.w
            }
        },
        {"flashColor",
            {
                g_config.esp.flashColor.Value.x,
                g_config.esp.flashColor.Value.y,
                g_config.esp.flashColor.Value.z,
                g_config.esp.flashColor.Value.w
            }
        },
        {"hkColor",
            {
                g_config.esp.hkColor.Value.x,
                g_config.esp.hkColor.Value.y,
                g_config.esp.hkColor.Value.z,
                g_config.esp.hkColor.Value.w
            }
        },
        {"scopedColor",
            {
                g_config.esp.scopedColor.Value.x,
                g_config.esp.scopedColor.Value.y,
                g_config.esp.scopedColor.Value.z,
                g_config.esp.scopedColor.Value.w
            }
        },
        {"weaponNameColor",
            {
                g_config.esp.weaponNameColor.Value.x,
                g_config.esp.weaponNameColor.Value.y,
                g_config.esp.weaponNameColor.Value.z,
                g_config.esp.weaponNameColor.Value.w
            }
        },
        {"defuserColor",
            {
                g_config.esp.defuserColor.Value.x,
                g_config.esp.defuserColor.Value.y,
                g_config.esp.defuserColor.Value.z,
                g_config.esp.defuserColor.Value.w
            }
        },
        {"defusingColor",
            {
                g_config.esp.defusingColor.Value.x,
                g_config.esp.defusingColor.Value.y,
                g_config.esp.defusingColor.Value.z,
                g_config.esp.defusingColor.Value.w
            }
        },
        {"kevlarColor",
            {
            g_config.esp.kevlarColor.Value.x,
            g_config.esp.kevlarColor.Value.y,
            g_config.esp.kevlarColor.Value.z,
            g_config.esp.kevlarColor.Value.w
            }
        }
    };

    auto& jShowFlags = j["ESP_Players"]["show_flags"];
    for (const auto& meta : k_flag_meta)
        jShowFlags[meta.json_key.data()] = (g_config.esp.player.uShowFlags & meta.flag) != 0;

    // ----- Visuals -----
    j["Visuals"] =
    {
        {"bDrawFovCircle", g_config.visuals.bDrawFovCircle},
        {"bDrawSnapLines", g_config.visuals.bDrawSnapLines},
    };

    std::ofstream file(defaultCfgPath + cfgName);
    if (!file.is_open()) return;

    file << j.dump(4);
}

void cfg::Delete(const std::string& cfgName)
{
    auto filePath = std::filesystem::path(defaultCfgPath + cfgName);

    if (std::filesystem::exists(filePath))
        std::filesystem::remove(filePath);
}

void cfg::SetDefault(const std::string& cfgName)
{
    std::ofstream file(k_default_cfg_path, std::ios::trunc);
    if (file.is_open())
        file << cfgName;
}

std::string cfg::GetDefault()
{
    std::ifstream file(k_default_cfg_path);
    if (!file.is_open())
        return {};

    std::string name;
    std::getline(file, name);
    return name;
}

bool cfg::LoadDefault()
{
    const std::string default_name = cfg::GetDefault();
    if (default_name.empty())
        return false;

    cfg::Load(default_name);
    return true;
}