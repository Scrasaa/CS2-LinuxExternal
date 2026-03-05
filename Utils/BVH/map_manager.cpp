#include "map_manager.h"

#include <cstdio>
#include <fstream>

#include "map_parser.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────
//  Internal helper — find CS2 maps directory
//
//  When running as root via sudo, $HOME is /root
//  but the actual user's Steam is under /home/<user>.
//  SUDO_USER contains the real username in that case.
// ─────────────────────────────────────────────

static fs::path find_maps_dir()
{
    // Build a list of home directories to try
    std::vector<fs::path> a_homes;

    const char* psz_sudo_user = std::getenv("SUDO_USER");
    if (psz_sudo_user && psz_sudo_user[0] != '\0')
        a_homes.push_back(fs::path("/home") / psz_sudo_user);

    const char* psz_home = std::getenv("HOME");
    if (psz_home && psz_home[0] != '\0')
        a_homes.push_back(fs::path(psz_home));

    // Also scan all users in /home as a fallback
    if (fs::exists("/home"))
    {
        for (const fs::directory_entry& entry : fs::directory_iterator("/home"))
        {
            if (entry.is_directory())
                a_homes.push_back(entry.path());
        }
    }

    static constexpr const char* k_cs2_maps =
        "Counter-Strike Global Offensive/game/csgo/maps";

    for (const fs::path& home : a_homes)
    {
        const fs::path a_candidates[] =
        {
            home / ".local/share/Steam/steamapps/common" / k_cs2_maps,
            home / ".steam/steam/steamapps/common"       / k_cs2_maps,
            home / ".steam/debian-installation/steamapps/common" / k_cs2_maps,
        };

        for (const fs::path& p : a_candidates)
        {
            if (fs::exists(p))
            {
                printf("[map] found maps dir: '%s'\n", p.string().c_str());
                return p;
            }
        }
    }

    printf("[map] could not find CS2 maps directory\n");
    return {};
}

// ─────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────

MapManager::MapManager(
    uintptr_t                                           p_global_vars,
    std::function<uintptr_t(uintptr_t)>                fn_read_ptr,
    std::function<std::string(uintptr_t, std::size_t)> fn_read_string,
    std::string                                         sz_s2v_binary)
    : m_p_global_vars  (p_global_vars)
    , m_fn_read_ptr    (std::move(fn_read_ptr))
    , m_fn_read_string (std::move(fn_read_string))
    , m_sz_s2v_binary  (std::move(sz_s2v_binary))
{}

// ─────────────────────────────────────────────
//  Public
// ─────────────────────────────────────────────

void MapManager::update()
{
    const std::string sz_map = read_map_name();

    // Only skip if map is the same AND already loaded successfully
    if (sz_map.empty() || (sz_map == m_sz_current_map && m_b_loaded))
        return;

    printf("[map] map changed: '%s'\n", sz_map.c_str());

    m_sz_current_map = sz_map;
    m_bvh            = Bvh{};
    m_b_loaded       = false;

    const fs::path path_geo = find_geo_path(sz_map);
    const fs::path path_vpk = find_vpk_path(sz_map);

    printf("[map] geo: '%s' | exists: %d\n", path_geo.string().c_str(), (int)fs::exists(path_geo));
    printf("[map] vpk: '%s' | exists: %d\n", path_vpk.string().c_str(), (int)fs::exists(path_vpk));

    if (fs::exists(path_geo))
    {
        // Re-parse if VPK is newer than cached .geo (CS2 updated)
        if (fs::exists(path_vpk) && fs::last_write_time(path_vpk) > fs::last_write_time(path_geo))
        {
            printf("[map] VPK newer than .geo — reparsing\n");
            fs::remove(path_geo);
        }
        else
        {
            m_b_loaded = load_geo(path_geo);
            printf("[map] load result: %d\n", (int)m_b_loaded);
            return;
        }
    }

    if (path_vpk.empty() || !fs::exists(path_vpk))
    {
        printf("[map] VPK not found — cannot parse\n");
        return;
    }

    const fs::path path_geo_out = path_vpk.parent_path() / (sz_map + ".geo");
    printf("[map] parsing vpk → '%s'\n", path_geo_out.string().c_str());

    const bool b_parse_ok = parse_map_to_geo(m_sz_s2v_binary, path_vpk, path_geo_out);
    printf("[map] parse result: %d\n", (int)b_parse_ok);

    if (fs::exists(path_geo_out))
    {
        m_b_loaded = load_geo(path_geo_out);
        printf("[map] load after parse: %d\n", (int)m_b_loaded);
    }
}

bool MapManager::is_visible(const glm::vec3& v3_start, const glm::vec3& v3_end) const
{
    if (!m_b_loaded)
        return true;

    return m_bvh.has_line_of_sight(v3_start, v3_end);
}

// ─────────────────────────────────────────────
//  Private
// ─────────────────────────────────────────────

std::string MapManager::read_map_name() const
{
    const uintptr_t p_base     = m_fn_read_ptr(m_p_global_vars);
    const uintptr_t p_name_str = m_fn_read_ptr(p_base + 0x198);
    if (!p_base || !p_name_str)
        return {};

    std::string sz_name = m_fn_read_string(p_name_str, 64u);

    // Strip "maps/" prefix CS2 sometimes includes
    const auto n_slash = sz_name.rfind('/');
    if (n_slash != std::string::npos)
        sz_name = sz_name.substr(n_slash + 1);

    return sz_name;
}

fs::path MapManager::find_geo_path(const std::string& sz_map_name)
{
    const fs::path path_maps = find_maps_dir();
    if (path_maps.empty())
        return {};
    return path_maps / (sz_map_name + ".geo");
}

fs::path MapManager::find_vpk_path(const std::string& sz_map_name)
{
    const fs::path path_maps = find_maps_dir();
    if (path_maps.empty())
        return {};
    return path_maps / (sz_map_name + ".vpk");
}

bool MapManager::load_geo(const fs::path& path_geo)
{
    std::ifstream f_geo(path_geo, std::ios::binary);
    if (!f_geo.is_open())
        return false;

    auto fn_read_u64 = [&]() -> uint64_t
    {
        uint64_t n_val = 0u;
        f_geo.read(reinterpret_cast<char*>(&n_val), sizeof(n_val));
        return n_val;
    };

    auto fn_read_f32 = [&]() -> float
    {
        float f_val = 0.0f;
        f_geo.read(reinterpret_cast<char*>(&f_val), sizeof(f_val));
        return f_val;
    };

    const uint64_t n_tri_count = fn_read_u64();
    printf("[map] triangle count: %llu\n", (unsigned long long)n_tri_count);

    if (n_tri_count == 0u)
        return false;

    for (uint64_t i = 0u; i < n_tri_count; ++i)
    {
        const glm::vec3 v3_v0{ fn_read_f32(), fn_read_f32(), fn_read_f32() };
        const glm::vec3 v3_v1{ fn_read_f32(), fn_read_f32(), fn_read_f32() };
        const glm::vec3 v3_v2{ fn_read_f32(), fn_read_f32(), fn_read_f32() };
        m_bvh.insert(Triangle{ v3_v0, v3_v1, v3_v2 });
    }

    m_bvh.build();
    return true;
}