//
// Created by scrasa on 06.03.26.
//

#include <fstream>
#include "Config.h"

#include "../Thirdparty/json.h"

using json = nlohmann::json;

constexpr auto defaultCfgPath  = "/home/scrasa/.config/cs2-external/";

//------------------------ Helper Functions

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

    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
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
    g_config.aimbot.bEnable        = jAimbot.value("bEnable", g_config.aimbot.bEnable);
    g_config.aimbot.bVisible        = jAimbot.value("bVisible", g_config.aimbot.bVisible);
    g_config.aimbot.fRadius         = jAimbot.value("fRadius", g_config.aimbot.fRadius);
    g_config.aimbot.fSmoothness         = jAimbot.value("fSmoothness", g_config.aimbot.fSmoothness);
    g_config.aimbot.bAutoShoot      = jAimbot.value("bAutoShoot", g_config.aimbot.bAutoShoot);

    // ----- ESP: Players -----
    auto jPlayerESP = j.value("ESP_Players", json::object());
    g_config.esp.player.bEnable   = jPlayerESP.value("bEnable", g_config.esp.player.bEnable);
    g_config.esp.player.bDraw2DBox   = jPlayerESP.value("bDraw2DBox", g_config.esp.player.bDraw2DBox);
    g_config.esp.player.bName   = jPlayerESP.value("bName", g_config.esp.player.bName);
    g_config.esp.player.bTeam   = jPlayerESP.value("bTeam", g_config.esp.player.bTeam);
    g_config.esp.player.bHealth   = jPlayerESP.value("bHealth", g_config.esp.player.bHealth);
    g_config.esp.player.bSkeleton   = jPlayerESP.value("bSkeleton", g_config.esp.player.bSkeleton);

    g_config.esp.boxColorEnemy =  JsonToImColor(jPlayerESP.value("boxColorEnemy", ImColorToJson(g_config.esp.boxColorEnemy)));
    g_config.esp.boxColorTeam =  JsonToImColor(jPlayerESP.value("boxColorTeam", ImColorToJson(g_config.esp.boxColorTeam)));

    // ----- Visuals -----
    auto jVisuals = j.value("Visuals", json::object());
    g_config.visuals.bDrawFovCircle       = jVisuals.value("bDrawFovCircle", g_config.visuals.bDrawFovCircle);
    g_config.visuals.bDrawSnapLines          = jVisuals.value("bDrawSnapLines", g_config.visuals.bDrawSnapLines);
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

    // ----- ESP: Players -----
    j["ESP_Players"] =
    {
        {"bEnable", g_config.esp.player.bEnable},
        {"bSkeleton", g_config.esp.player.bSkeleton},
        {"bName", g_config.esp.player.bName},
        {"bWeapon", g_config.esp.player.bWeapon},
        {"bAmmo", g_config.esp.player.bAmmo},
        {"bHealth", g_config.esp.player.bHealth},
        {"bDraw2DBox", g_config.esp.player.bDraw2DBox},
        {"bVisible", g_config.esp.player.bVisible},
        {"bTeam", g_config.esp.player.bTeam}
    };

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