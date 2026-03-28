#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  map_parser.h
//
//  Calls Source2Viewer-CLI to extract world_physics from a VPK,
//  parses the resulting DMX files, triangulates faces, and writes a .geo file.
//
//  Requires: Source2Viewer-CLI in PATH or next to your executable.
//  Get it from: https://github.com/ValveResourceFormat/ValveResourceFormat
//
//  Port of parser.rs from avitran0/deadlocked.
// ─────────────────────────────────────────────────────────────────────────────

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Bvh.h"
#include "dmx_parser.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────
//  Run a shell command, return exit code
// ─────────────────────────────────────────────

inline int run_command(const std::string& sz_cmd)
{
    return std::system(sz_cmd.c_str());
}

// ─────────────────────────────────────────────
//  Write a flat .geo file from a built BVH
//
//  Format (little-endian):
//    uint64   triangle_count
//    float32  v0x v0y v0z v1x v1y v1z v2x v2y v2z   (x triangle_count)
// ─────────────────────────────────────────────

inline bool write_geo(const Bvh& bvh, const fs::path& path_out)
{
    std::ofstream f(path_out, std::ios::binary);
    if (!f.is_open())
        return false;

    auto fn_write = [&](const auto& val)
    {
        f.write(reinterpret_cast<const char*>(&val), sizeof(val));
    };

    const auto n_count = static_cast<uint64_t>(bvh.tris().size());
    fn_write(n_count);

    for (const Triangle& tri : bvh.tris())
    {
        fn_write(tri.m_v3V0.x); fn_write(tri.m_v3V0.y); fn_write(tri.m_v3V0.z);
        fn_write(tri.m_v3V1.x); fn_write(tri.m_v3V1.y); fn_write(tri.m_v3V1.z);
        fn_write(tri.m_v3V2.x); fn_write(tri.m_v3V2.y); fn_write(tri.m_v3V2.z);
    }

    return true;
}

// ─────────────────────────────────────────────
//  Parse a single DMX file into the BVH
// ─────────────────────────────────────────────

enum class DmxFileType { Hull, Phys };

inline void parse_dmx_file(const fs::path& path_dmx, DmxFileType e_type, Bvh& bvh_out)
{
    // Read entire file into memory
    std::ifstream f(path_dmx, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return;

    const std::size_t n_size = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> a_data(n_size);
    f.read(reinterpret_cast<char*>(a_data.data()), static_cast<std::streamsize>(n_size));

    std::unordered_map<std::string, dmx::Element> elements;
    try
    {
        elements = dmx::parse(a_data.data(), n_size);
    }
    catch (...)
    {
        return;
    }

    // Check material flag — only "$" prefixed materials are solid
    const auto it_mat = elements.find("DmeMaterial_material");
    if (it_mat == elements.end())
        return;

    const auto* p_mname = it_mat->second.get("mtlName");
    if (!p_mname)
        return;

    const auto* p_str = std::get_if<std::string>(p_mname);
    if (!p_str || p_str->empty() || (*p_str)[0] != '$')
        return;

    // Get vertex positions
    const auto it_vert = elements.find("DmeVertexData_bind");
    if (it_vert == elements.end())
        return;

    const auto* p_vattr = it_vert->second.get("position$0");
    if (!p_vattr)
        return;

    const auto* p_vertices = std::get_if<dmx::AttrVec3Array>(p_vattr);
    if (!p_vertices || p_vertices->empty())
        return;

    // Get face indices
    std::vector<std::vector<int32_t>> a_faces;

    if (e_type == DmxFileType::Hull)
    {
        const auto it_face = elements.find("DmeFaceSet_hull faces");
        if (it_face == elements.end())
            return;

        const auto* p_iattr = it_face->second.get("faces");
        if (!p_iattr)
            return;

        const auto* p_indices = std::get_if<dmx::AttrIntArray>(p_iattr);
        if (!p_indices)
            return;

        // Split on -1 sentinel
        std::vector<int32_t> a_current;
        for (const int32_t n_idx : *p_indices)
        {
            if (n_idx == -1)
            {
                if (!a_current.empty())
                {
                    a_faces.push_back(std::move(a_current));
                    a_current.clear();
                }
            }
            else
            {
                a_current.push_back(n_idx);
            }
        }
        if (!a_current.empty())
            a_faces.push_back(std::move(a_current));
    }
    else // Phys
    {
        const auto* p_iattr = it_vert->second.get("position$0Indices");
        if (!p_iattr)
            return;

        const auto* p_indices = std::get_if<dmx::AttrIntArray>(p_iattr);
        if (!p_indices)
            return;

        for (std::size_t i = 0u; i + 2u < p_indices->size(); i += 3u)
            a_faces.push_back({ (*p_indices)[i], (*p_indices)[i+1], (*p_indices)[i+2] });
    }

    // Triangulate and insert
    for (const std::vector<int32_t>& face : a_faces)
    {
        if (face.size() < 3u)
            continue;

        // Validate all indices
        bool b_valid = true;
        for (const int32_t n_idx : face)
        {
            if (n_idx < 0 || static_cast<std::size_t>(n_idx) >= p_vertices->size())
            {
                b_valid = false;
                break;
            }
        }
        if (!b_valid)
            continue;

        if (face.size() == 3u)
        {
            bvh_out.insert(Triangle{
                (*p_vertices)[static_cast<std::size_t>(face[0])],
                (*p_vertices)[static_cast<std::size_t>(face[1])],
                (*p_vertices)[static_cast<std::size_t>(face[2])]
            });
        }
        else
        {
            // Fan triangulation for quads / n-gons
            for (std::size_t i = 1u; i < face.size() - 1u; ++i)
            {
                bvh_out.insert(Triangle{
                    (*p_vertices)[static_cast<std::size_t>(face[0])],
                    (*p_vertices)[static_cast<std::size_t>(face[i])],
                    (*p_vertices)[static_cast<std::size_t>(face[i + 1u])]
                });
            }
        }
    }
}

// ─────────────────────────────────────────────
//  Parse one map VPK → write .geo
//
//  sz_s2v_binary:  path to Source2Viewer-CLI, or "Source2Viewer-CLI" if in PATH
//  path_vpk:       e.g. ".../maps/de_dust2.vpk"
//  path_geo_out:   where to write the .geo file
// ─────────────────────────────────────────────

inline bool parse_map_to_geo(
    const std::string& sz_s2v_binary,
    const fs::path&    path_vpk,
    const fs::path&    path_geo_out)
{
    const std::string sz_map_name = path_vpk.stem().string();

    // Temp dir for extracted DMX files
    const fs::path path_tmp = fs::temp_directory_path() / ("cs2_geo_" + sz_map_name);
    fs::create_directories(path_tmp);

    // Call Source2Viewer-CLI to extract world_physics
    const std::string sz_cmd =
        "\"" + sz_s2v_binary + "\""
        " -i \"" + path_vpk.string() + "\""
        " -d"
        " -o \"" + path_tmp.string() + "\""
        " -f \"maps/" + sz_map_name + "/world_physics.vmdl_c\""
        " 2>/dev/null";

    if (run_command(sz_cmd) != 0)
    {
        fs::remove_all(path_tmp);
        return false;
    }

    // Walk extracted files
    const fs::path path_geom = path_tmp / "maps" / sz_map_name;
    if (!fs::exists(path_geom))
    {
        fs::remove_all(path_tmp);
        return false;
    }

    Bvh bvh;
    for (const fs::directory_entry& entry : fs::directory_iterator(path_geom))
    {
        if (!entry.is_regular_file())
            continue;

        const std::string sz_fname = entry.path().filename().string();
        DmxFileType e_type;

        if (sz_fname.find("world_physics_hull") != std::string::npos)
            e_type = DmxFileType::Hull;
        else if (sz_fname.find("world_physics_phys") != std::string::npos)
            e_type = DmxFileType::Phys;
        else
            continue;

        parse_dmx_file(entry.path(), e_type, bvh);
    }

    bvh.rebuild();

    const bool b_ok = write_geo(bvh, path_geo_out);

    fs::remove_all(path_tmp); // clean up temp files
    return b_ok;
}