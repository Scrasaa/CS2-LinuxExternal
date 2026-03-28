#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>

static constexpr uint32_t k_max_leaf_primitives = 8u;

// ─────────────────────────────────────────────────────────────────────────────
//  Aabb
// ─────────────────────────────────────────────────────────────────────────────

struct Aabb
{
    glm::vec3 m_v3Min{ std::numeric_limits<float>::max()    };
    glm::vec3 m_v3Max{ std::numeric_limits<float>::lowest() };

    void expand(const glm::vec3& v3Point) noexcept
    {
        m_v3Min = glm::min(m_v3Min, v3Point);
        m_v3Max = glm::max(m_v3Max, v3Point);
    }

    [[nodiscard]] Aabb merge(const Aabb& other) const noexcept
    {
        Aabb result;
        result.m_v3Min = glm::min(m_v3Min, other.m_v3Min);
        result.m_v3Max = glm::max(m_v3Max, other.m_v3Max);
        return result;
    }

    [[nodiscard]] glm::vec3 centroid() const noexcept
    {
        return (m_v3Min + m_v3Max) * 0.5f;
    }

    [[nodiscard]] bool ray_intersect(
        const glm::vec3& v3Origin,
        const glm::vec3& v3InvDir,
        const float      f_max_t) const noexcept
    {
        const glm::vec3 v3T1   = (m_v3Min - v3Origin) * v3InvDir;
        const glm::vec3 v3T2   = (m_v3Max - v3Origin) * v3InvDir;
        const glm::vec3 v3TMin = glm::min(v3T1, v3T2);
        const glm::vec3 v3TMax = glm::max(v3T1, v3T2);

        const float f_t_min = std::max({ v3TMin.x, v3TMin.y, v3TMin.z });
        const float f_t_max = std::min({ v3TMax.x, v3TMax.y, v3TMax.z });

        return f_t_min <= f_t_max && f_t_min <= f_max_t && f_t_max >= 0.0f;
    }
};

static_assert(std::is_trivially_copyable_v<Aabb>);
static_assert(sizeof(Aabb) == 24u);

// ─────────────────────────────────────────────────────────────────────────────
//  Triangle
// ─────────────────────────────────────────────────────────────────────────────

struct Triangle
{
    glm::vec3 m_v3V0;
    glm::vec3 m_v3V1;
    glm::vec3 m_v3V2;

    Triangle() = default;
    Triangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) noexcept
        : m_v3V0(v0), m_v3V1(v1), m_v3V2(v2) {}

    [[nodiscard]] glm::vec3 centroid() const noexcept
    {
        return (m_v3V0 + m_v3V1 + m_v3V2) * (1.0f / 3.0f);
    }

    [[nodiscard]] Aabb aabb() const noexcept
    {
        Aabb result;
        result.expand(m_v3V0);
        result.expand(m_v3V1);
        result.expand(m_v3V2);
        return result;
    }

    // Möller–Trumbore — returns { t, u, v } on hit
    [[nodiscard]] std::optional<std::tuple<float, float, float>> ray_intersect(
        const glm::vec3& v3Origin,
        const glm::vec3& v3Dir) const noexcept
    {
        static constexpr float k_epsilon = 1e-6f;

        const glm::vec3 v3E1 = m_v3V1 - m_v3V0;
        const glm::vec3 v3E2 = m_v3V2 - m_v3V0;
        const glm::vec3 v3H  = glm::cross(v3Dir, v3E2);
        const float     f_a  = glm::dot(v3E1, v3H);

        if (f_a > -k_epsilon && f_a < k_epsilon)
            return std::nullopt;

        const float     f_f  = 1.0f / f_a;
        const glm::vec3 v3S  = v3Origin - m_v3V0;
        const float     f_u  = f_f * glm::dot(v3S, v3H);

        if (f_u < 0.0f || f_u > 1.0f)
            return std::nullopt;

        const glm::vec3 v3Q = glm::cross(v3S, v3E1);
        const float     f_v = f_f * glm::dot(v3Dir, v3Q);

        if (f_v < 0.0f || f_u + f_v > 1.0f)
            return std::nullopt;

        const float f_t = f_f * glm::dot(v3E2, v3Q);
        if (f_t <= k_epsilon)
            return std::nullopt;

        return std::make_tuple(f_t, f_u, f_v);
    }
};

static_assert(std::is_trivially_copyable_v<Triangle>);
static_assert(sizeof(Triangle) == 9u * sizeof(float));

// ─────────────────────────────────────────────────────────────────────────────
//  BvhNode  —  32 bytes, trivially copyable, directly memcpy-serializable
//
//  Layout encoding (branch vs leaf):
//
//    Branch  (m_n_prim_count == 0):
//        left  child  →  implicit: node index + 1  (depth-first linear layout)
//        right child  →  m_n_offset
//
//    Leaf    (m_n_prim_count  > 0):
//        primitives   →  m_v_prim_indices[m_n_offset .. m_n_offset + m_n_prim_count]
//
// ─────────────────────────────────────────────────────────────────────────────

struct BvhNode
{
    Aabb     m_aabb;          // 24 bytes
    uint32_t m_n_prim_count;  //  4 bytes — 0 = branch, >0 = leaf
    uint32_t m_n_offset;      //  4 bytes — branch: right child index | leaf: first prim index

    [[nodiscard]] bool is_leaf()   const noexcept { return m_n_prim_count > 0u; }
    [[nodiscard]] bool is_branch() const noexcept { return m_n_prim_count == 0u; }
};

static_assert(std::is_trivially_copyable_v<BvhNode>);
static_assert(sizeof(BvhNode) == 32u, "BvhNode must be exactly 32 bytes");

// ─────────────────────────────────────────────────────────────────────────────
//  Bvh
// ─────────────────────────────────────────────────────────────────────────────

class Bvh
{
public:
    // ── Construction ─────────────────────────────────────────────────────────

    // Build from a non-owning span — copies triangles internally
    void build(std::span<const Triangle> tris)
    {
        m_v_tris.assign(tris.begin(), tris.end());
        rebuild();
    }

    // Build from an owned vector — zero-copy move
    void build(std::vector<Triangle> v_tris)
    {
        m_v_tris = std::move(v_tris);
        rebuild();
    }

    // ── Query ─────────────────────────────────────────────────────────────────

    // Returns true if the segment [v3Start, v3End] is unobstructed
    [[nodiscard]] bool has_line_of_sight(
        const glm::vec3& v3Start,
        const glm::vec3& v3End) const noexcept
    {
        if (m_v_nodes.empty())
            return true;

        const glm::vec3 v3Dir  = v3End - v3Start;
        const float     f_dist = glm::length(v3Dir);
        if (f_dist < 1e-6f)
            return true;

        const glm::vec3 v3Dir_norm = v3Dir / f_dist;
        const glm::vec3 v3Inv_dir  = 1.0f / v3Dir_norm;

        return !traverse(v3Start, v3Dir_norm, v3Inv_dir, f_dist);
    }

    // ── Serialization accessors ───────────────────────────────────────────────

    std::vector<BvhNode>&   nodes()        noexcept { return m_v_nodes;       }
    std::vector<uint32_t>&  prim_indices() noexcept { return m_v_prim_indices; }
    std::vector<Triangle>&  tris()         noexcept { return m_v_tris;        }

    [[nodiscard]] const std::vector<BvhNode>&   nodes()        const noexcept { return m_v_nodes;       }
    [[nodiscard]] const std::vector<uint32_t>&  prim_indices() const noexcept { return m_v_prim_indices; }
    [[nodiscard]] const std::vector<Triangle>&  tris()         const noexcept { return m_v_tris;        }

    void insert(const Triangle& tri)
    {
        m_v_tris.push_back(tri);
    }

    void insert(std::span<const Triangle> tris)
    {
        m_v_tris.insert(m_v_tris.end(), tris.begin(), tris.end());
    }

private:
    std::vector<BvhNode>   m_v_nodes;
    std::vector<uint32_t>  m_v_prim_indices;
    std::vector<Triangle>  m_v_tris;

    // ── Build ─────────────────────────────────────────────────────────────────
public:
    void rebuild()
    {
        m_v_nodes.clear();
        m_v_prim_indices.clear();

        if (m_v_tris.empty())
            return;

        // A full binary tree with n leaves has at most 2n − 1 nodes.
        // With k_max_leaf_primitives > 1, leaves are fewer, so this is a safe upper bound.
        m_v_nodes.reserve(2u * m_v_tris.size());
        m_v_prim_indices.reserve(m_v_tris.size());

        std::vector<uint32_t> indices(m_v_tris.size());
        std::iota(indices.begin(), indices.end(), 0u);

        build_node(indices.data(), static_cast<uint32_t>(indices.size()));
    }
private:
    // Recursive depth-first builder.
    // Reserves a node slot first, recurses into children, then fills the slot.
    // Left child is always at (this_node_index + 1) in m_v_nodes (DFS order).
    // Right child index is stored in m_n_offset for branch nodes.
    void build_node(uint32_t* p_idx, const uint32_t n_count)
    {
        const uint32_t n_node_idx = static_cast<uint32_t>(m_v_nodes.size());
        m_v_nodes.emplace_back(); // slot — filled in at the end of this call

        // Compute the combined AABB for all primitives in this node
        Aabb aabb;
        for (uint32_t i = 0u; i < n_count; ++i)
            aabb = aabb.merge(m_v_tris[p_idx[i]].aabb());

        // ── Leaf ─────────────────────────────────────────────────────────────
        if (n_count <= k_max_leaf_primitives)
        {
            BvhNode& node       = m_v_nodes[n_node_idx];
            node.m_aabb         = aabb;
            node.m_n_prim_count = n_count;
            node.m_n_offset     = static_cast<uint32_t>(m_v_prim_indices.size());
            for (uint32_t i = 0u; i < n_count; ++i)
                m_v_prim_indices.push_back(p_idx[i]);
            return;
        }

        // ── Choose split axis — longest axis of the centroid AABB ────────────
        Aabb centroid_aabb;
        for (uint32_t i = 0u; i < n_count; ++i)
            centroid_aabb.expand(m_v_tris[p_idx[i]].centroid());

        const glm::vec3 v3Extent = centroid_aabb.m_v3Max - centroid_aabb.m_v3Min;
        const int n_axis = (v3Extent.y > v3Extent.x && v3Extent.y > v3Extent.z) ? 1
                         : (v3Extent.z > v3Extent.x)                             ? 2
                         :                                                          0;

        // nth_element is O(n) — no need for a full O(n log n) sort here
        const uint32_t n_mid = n_count / 2u;
        std::nth_element(p_idx, p_idx + n_mid, p_idx + n_count,
            [&](const uint32_t a, const uint32_t b) noexcept
            {
                return m_v_tris[a].centroid()[n_axis] < m_v_tris[b].centroid()[n_axis];
            });

        // ── Branch ───────────────────────────────────────────────────────────
        // Left subtree starts immediately after this node (index + 1)
        build_node(p_idx,           n_mid);
        const uint32_t n_right = static_cast<uint32_t>(m_v_nodes.size());
        build_node(p_idx + n_mid,   n_count - n_mid);

        // Re-fetch by index — vector may have grown during recursion
        BvhNode& node       = m_v_nodes[n_node_idx];
        node.m_aabb         = aabb;
        node.m_n_prim_count = 0u;
        node.m_n_offset     = n_right; // right child; left = n_node_idx + 1 (implicit)
    }

    // ── Traversal ─────────────────────────────────────────────────────────────

    // Iterative stack-based traversal — no recursion, no heap allocation.
    // Returns true on the first triangle hit within [0, f_max_t].
    [[nodiscard]] bool traverse(
        const glm::vec3& v3Origin,
        const glm::vec3& v3Dir,
        const glm::vec3& v3Inv_dir,
        const float      f_max_t) const noexcept
    {
        // Depth-64 stack covers trees of 10M+ primitives with room to spare
        uint32_t stack[64];
        int      n_top = 0;
        stack[n_top++] = 0u; // root is always node 0

        while (n_top > 0)
        {
            const uint32_t n_idx = stack[--n_top];
            const BvhNode& node  = m_v_nodes[n_idx];

            if (!node.m_aabb.ray_intersect(v3Origin, v3Inv_dir, f_max_t))
                continue;

            if (node.is_leaf())
            {
                const uint32_t n_end = node.m_n_offset + node.m_n_prim_count;
                for (uint32_t i = node.m_n_offset; i < n_end; ++i)
                {
                    const auto hit = m_v_tris[m_v_prim_indices[i]]
                                         .ray_intersect(v3Origin, v3Dir);
                    if (hit && std::get<0>(*hit) <= f_max_t)
                        return true; // early-exit on first obstruction
                }
            }
            else
            {
                // Push right first so left is popped first (LIFO → left tested first)
                stack[n_top++] = node.m_n_offset; // right
                stack[n_top++] = n_idx + 1u;       // left (always adjacent in DFS layout)
            }
        }

        return false;
    }
};