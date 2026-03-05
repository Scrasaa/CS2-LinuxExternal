#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  map_visibility.hpp
//
//  Drop-in visibility check for an external CS2 cheat.
//  Depends on: bvh.hpp (provided separately), GLM.
//
//  Workflow:
//    1. Run the Rust parser tool once → produces de_dust2.geo, de_mirage.geo ...
//       (stored in <steam>/steamapps/common/Counter-Strike Global Offensive/game/csgo/maps/)
//    2. Call MapVisibility::load("de_dust2") at startup / map load.
//    3. Call has_line_of_sight(local_head_pos, enemy_head_pos) per entity per frame.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

#include "Bvh.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────
//  Locate the CS2 maps directory automatically
//  via Steam's libraryfolders.vdf.
// ─────────────────────────────────────────────

inline fs::path find_cs2_maps_dir()
{
    const char* pszHome = std::getenv("HOME");
    if (!pszHome)
        throw std::runtime_error("HOME not set");

    // Windows: %LOCALAPPDATA%\Steam  or  C:\Program Files (x86)\Steam
    // Linux:   ~/.steam/steam
#if defined(_WIN32)
    const fs::path pathSteam = fs::path(std::getenv("PROGRAMFILES(X86)")) / "Steam";
#else
    const fs::path pathSteam = fs::path(pszHome) / ".steam/steam";
#endif

    const fs::path pathVdf = pathSteam / "config/libraryfolders.vdf";
    std::ifstream  fVdf(pathVdf);
    if (!fVdf.is_open())
        throw std::runtime_error("cannot open libraryfolders.vdf");

    std::string szLine;
    while (std::getline(fVdf, szLine))
    {
        if (szLine.find("\"path\"") == std::string::npos)
            continue;

        // Extract path value from:  "path"   "C:\\Program Files\\..."
        const auto nFirst = szLine.rfind('"', szLine.rfind('"') - 1);
        const auto nLast  = szLine.rfind('"');
        if (nFirst == std::string::npos || nFirst == nLast)
            continue;

        fs::path pathLib = szLine.substr(nFirst + 1, nLast - nFirst - 1);
        fs::path pathCs2 = pathLib
            / "steamapps/common/Counter-Strike Global Offensive/game/csgo/maps";

        if (fs::exists(pathCs2))
            return pathCs2;
    }

    throw std::runtime_error("CS2 maps directory not found");
}

// ─────────────────────────────────────────────
//  Load a .geo file produced by the Rust parser.
//
//  Format (little-endian):
//    uint64  triangle_count
//    float32 v0x v0y v0z v1x v1y v1z v2x v2y v2z   (× triangle_count)
// ─────────────────────────────────────────────

inline Bvh load_geo(const fs::path& pathGeo)
{
    std::ifstream fGeo(pathGeo, std::ios::binary);
    if (!fGeo.is_open())
        throw std::runtime_error("cannot open: " + pathGeo.string());

    auto read_u64 = [&]() -> uint64_t
    {
        uint64_t nVal = 0u;
        fGeo.read(reinterpret_cast<char*>(&nVal), sizeof(nVal));
        return nVal;
    };

    auto read_f32 = [&]() -> float
    {
        float fVal = 0.0f;
        fGeo.read(reinterpret_cast<char*>(&fVal), sizeof(fVal));
        return fVal;
    };

    const uint64_t nTriCount = read_u64();

    Bvh bvh;
    bvh.triangles().reserve(nTriCount);  // optional: add reserve() to Bvh if desired

    for (uint64_t i = 0u; i < nTriCount; ++i)
    {
        const glm::vec3 v3V0{ read_f32(), read_f32(), read_f32() };
        const glm::vec3 v3V1{ read_f32(), read_f32(), read_f32() };
        const glm::vec3 v3V2{ read_f32(), read_f32(), read_f32() };
        bvh.insert(Triangle{ v3V0, v3V1, v3V2 });
    }

    bvh.build();
    return bvh;
}

// ─────────────────────────────────────────────
//  MapVisibility — owns the BVH for the current map.
//  Create one instance, keep it alive for the match.
// ─────────────────────────────────────────────

class MapVisibility
{
public:
    // Call this when you detect a map load/change.
    // map_name: e.g. "de_dust2"  (no extension)
    bool load(const std::string& sz_map_name)
    {
        try
        {
            const fs::path path_maps = find_cs2_maps_dir();
            const fs::path path_geo  = path_maps / (sz_map_name + ".geo");

            if (!fs::exists(path_geo))
                return false;   // .geo not generated yet — run the Rust tool first

            m_bvh       = load_geo(path_geo);
            m_b_loaded  = true;
            return true;
        }
        catch (...)
        {
            m_b_loaded = false;
            return false;
        }
    }

    // Returns true if the straight line between the two world-space positions
    // is unobstructed by static map geometry.
    //
    // Typical call site (per enemy entity, per frame):
    //   glm::vec3 v3Local = local_player.get_eye_pos();
    //   glm::vec3 v3Enemy = enemy.get_eye_pos();
    //   bool b_visible = g_map_vis.has_line_of_sight(v3Local, v3Enemy);
    bool has_line_of_sight(const glm::vec3& v3Start, const glm::vec3& v3End) const
    {
        if (!m_b_loaded)
            return true;    // no geometry loaded → assume visible

        return m_bvh.has_line_of_sight(v3Start, v3End);
    }

    bool is_loaded() const { return m_b_loaded; }

private:
    Bvh  m_bvh;
    bool m_b_loaded = false;
};

// ─────────────────────────────────────────────
//  Usage example (in your main cheat loop)
// ─────────────────────────────────────────────

/*

// --- Startup / map change ---

MapVisibility g_map_vis;

void on_map_load(const std::string& sz_map)
{
    if (!g_map_vis.load(sz_map))
    {
        // .geo file missing — user needs to run the Rust parser tool first
    }
}

// --- Per-frame entity loop ---

void update_entities()
{
    const glm::vec3 v3_local_eye = local_player.get_eye_pos();

    for (auto& enemy : entity_list.enemies())
    {
        const glm::vec3 v3_enemy_eye = enemy.get_eye_pos();
        const bool b_visible = g_map_vis.has_line_of_sight(v3_local_eye, v3_enemy_eye);

        enemy.set_visible(b_visible);
    }
}

*/