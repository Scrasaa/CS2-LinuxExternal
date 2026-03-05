#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <glm/glm.hpp>

#include "Bvh.h"

class MapManager
{
public:
    MapManager() = default;
    MapManager(
        uintptr_t                                           p_global_vars,
        std::function<uintptr_t(uintptr_t)>                fn_read_ptr,
        std::function<std::string(uintptr_t, std::size_t)> fn_read_string,
        std::string                                         sz_s2v_binary = "Source2Viewer-CLI");

    // Call once per frame — detects map changes and reloads BVH automatically.
    void update();

    // Returns true if the line between the two positions is unobstructed.
    // Returns true (assume visible) when no geometry is loaded.
    bool is_visible(const glm::vec3& v3_start, const glm::vec3& v3_end) const;

    const std::string& current_map() const { return m_sz_current_map; }
    bool               is_loaded()   const { return m_b_loaded; }

private:
    std::string read_map_name() const;
    bool        load_geo(const std::filesystem::path& path_geo);

    static std::filesystem::path find_geo_path(const std::string& sz_map_name);
    static std::filesystem::path find_vpk_path(const std::string& sz_map_name);

    uintptr_t                                           m_p_global_vars  = 0u;
    std::function<uintptr_t(uintptr_t)>                m_fn_read_ptr;
    std::function<std::string(uintptr_t, std::size_t)> m_fn_read_string;
    std::string                                         m_sz_s2v_binary  = "Source2Viewer-CLI";

    Bvh         m_bvh;
    std::string m_sz_current_map;
    bool        m_b_loaded = false;
};

extern MapManager g_map_manager;