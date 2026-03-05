#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

// Requires GLM: https://github.com/g-truc/glm
// Tested with C++17 or later.

static constexpr std::size_t k_max_leaf_primitives = 8u;

// ─────────────────────────────────────────────
//  AABB
// ─────────────────────────────────────────────

struct Aabb
{
    glm::vec3 m_v3Min{ std::numeric_limits<float>::max() };
    glm::vec3 m_v3Max{ std::numeric_limits<float>::lowest() };

    void expand(const glm::vec3& v3Point)
    {
        m_v3Min = glm::min(m_v3Min, v3Point);
        m_v3Max = glm::max(m_v3Max, v3Point);
    }

    Aabb merge(const Aabb& other) const
    {
        Aabb result;
        result.m_v3Min = glm::min(m_v3Min, other.m_v3Min);
        result.m_v3Max = glm::max(m_v3Max, other.m_v3Max);
        return result;
    }

    glm::vec3 centroid() const
    {
        return (m_v3Min + m_v3Max) * 0.5f;
    }

    // Returns true if the ray [origin, origin + dir * max_t] intersects this AABB.
    bool ray_intersect(const glm::vec3& v3Origin, const glm::vec3& v3InvDir, float fMaxT) const
    {
        const glm::vec3 v3T1 = (m_v3Min - v3Origin) * v3InvDir;
        const glm::vec3 v3T2 = (m_v3Max - v3Origin) * v3InvDir;

        const glm::vec3 v3TMin = glm::min(v3T1, v3T2);
        const glm::vec3 v3TMax = glm::max(v3T1, v3T2);

        const float fTMin = std::max({ v3TMin.x, v3TMin.y, v3TMin.z });
        const float fTMax = std::min({ v3TMax.x, v3TMax.y, v3TMax.z });

        return fTMin <= fTMax && fTMin <= fMaxT && fTMax >= 0.0f;
    }
};

// ─────────────────────────────────────────────
//  Triangle
// ─────────────────────────────────────────────

struct Triangle
{
    glm::vec3 m_v3V0;
    glm::vec3 m_v3V1;
    glm::vec3 m_v3V2;

    Triangle() = default;
    Triangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
        : m_v3V0(v0), m_v3V1(v1), m_v3V2(v2) {}

    glm::vec3 centroid() const
    {
        return (m_v3V0 + m_v3V1 + m_v3V2) * (1.0f / 3.0f);
    }

    Aabb aabb() const
    {
        Aabb result;
        result.expand(m_v3V0);
        result.expand(m_v3V1);
        result.expand(m_v3V2);
        return result;
    }

    // Möller–Trumbore intersection.
    // Returns { t, u, v } if hit, std::nullopt otherwise.
    std::optional<std::tuple<float, float, float>> ray_intersect(
        const glm::vec3& v3Origin, const glm::vec3& v3Dir) const
    {
        static constexpr float k_epsilon = 1e-6f;

        const glm::vec3 v3E1 = m_v3V1 - m_v3V0;
        const glm::vec3 v3E2 = m_v3V2 - m_v3V0;
        const glm::vec3 v3H  = glm::cross(v3Dir, v3E2);
        const float     fA   = glm::dot(v3E1, v3H);

        if (fA > -k_epsilon && fA < k_epsilon)
            return std::nullopt;

        const float     fF = 1.0f / fA;
        const glm::vec3 v3S = v3Origin - m_v3V0;
        const float     fU  = fF * glm::dot(v3S, v3H);

        if (fU < 0.0f || fU > 1.0f)
            return std::nullopt;

        const glm::vec3 v3Q = glm::cross(v3S, v3E1);
        const float     fV  = fF * glm::dot(v3Dir, v3Q);

        if (fV < 0.0f || fU + fV > 1.0f)
            return std::nullopt;

        const float fT = fF * glm::dot(v3E2, v3Q);
        if (fT <= k_epsilon)
            return std::nullopt;

        return std::make_tuple(fT, fU, fV);
    }
};

// ─────────────────────────────────────────────
//  BVH Node (internal storage detail)
// ─────────────────────────────────────────────

struct BvhNode
{
    Aabb              m_aabb;
    bool              m_bIsLeaf   = false;

    // Branch
    std::size_t       m_nLeft     = 0u;
    std::size_t       m_nRight    = 0u;

    // Leaf
    std::vector<std::size_t> m_aPrimitives;
};

// ─────────────────────────────────────────────
//  BVH
// ─────────────────────────────────────────────

class Bvh
{
public:
    void insert(const Triangle& tri)
    {
        m_aTriangles.push_back(tri);
    }

    void build()
    {
        m_aNodes.clear();

        if (m_aTriangles.empty())
        {
            m_bHasRoot = false;
            return;
        }

        std::vector<std::size_t> aPrimitives(m_aTriangles.size());
        for (std::size_t i = 0u; i < aPrimitives.size(); ++i)
            aPrimitives[i] = i;

        m_nRoot    = build_recursive(aPrimitives);
        m_bHasRoot = true;
    }

    // Returns true if the segment [v3Start, v3End] is unobstructed.
    bool has_line_of_sight(const glm::vec3& v3Start, const glm::vec3& v3End) const
    {
        if (!m_bHasRoot)
            return true;

        const glm::vec3 v3Dir      = v3End - v3Start;
        const float     fDistance  = glm::length(v3Dir);
        const glm::vec3 v3DirNorm  = v3Dir / fDistance;
        const glm::vec3 v3InvDir   = 1.0f / v3DirNorm;

        return !segment_intersects_node(m_nRoot, v3Start, v3DirNorm, v3InvDir, fDistance);
    }

    const std::vector<Triangle>& triangles() const { return m_aTriangles; }
    const std::vector<BvhNode>&  nodes()     const { return m_aNodes;     }

private:
    std::vector<Triangle>  m_aTriangles;
    std::vector<BvhNode>   m_aNodes;
    std::size_t            m_nRoot    = 0u;
    bool                   m_bHasRoot = false;

    // ── Build helpers ──────────────────────────────────────────────

    std::size_t build_recursive(std::vector<std::size_t>& aPrims)
    {
        // Leaf
        if (aPrims.size() <= k_max_leaf_primitives)
        {
            Aabb aabb;
            for (std::size_t nIdx : aPrims)
                aabb = aabb.merge(m_aTriangles[nIdx].aabb());

            BvhNode node;
            node.m_bIsLeaf    = true;
            node.m_aabb       = aabb;
            node.m_aPrimitives = aPrims;
            m_aNodes.push_back(std::move(node));
            return m_aNodes.size() - 1u;
        }

        // Determine the widest axis of centroid bounds
        Aabb aCentroidBounds;
        for (std::size_t nIdx : aPrims)
            aCentroidBounds.expand(m_aTriangles[nIdx].centroid());

        const glm::vec3 v3Extent = aCentroidBounds.m_v3Max - aCentroidBounds.m_v3Min;
        int nAxis = 0;
        if (v3Extent.y > v3Extent.x && v3Extent.y > v3Extent.z)
            nAxis = 1;
        else if (v3Extent.z > v3Extent.x)
            nAxis = 2;

        // Sort along chosen axis and split at midpoint
        std::sort(aPrims.begin(), aPrims.end(),
            [&](std::size_t a, std::size_t b)
            {
                return m_aTriangles[a].centroid()[nAxis]
                     < m_aTriangles[b].centroid()[nAxis];
            });

        const std::size_t nMid = aPrims.size() / 2u;
        std::vector<std::size_t> aLeft (aPrims.begin(), aPrims.begin() + nMid);
        std::vector<std::size_t> aRight(aPrims.begin() + nMid, aPrims.end());

        const std::size_t nLeft  = build_recursive(aLeft);
        const std::size_t nRight = build_recursive(aRight);

        BvhNode node;
        node.m_bIsLeaf = false;
        node.m_nLeft   = nLeft;
        node.m_nRight  = nRight;
        node.m_aabb    = m_aNodes[nLeft].m_aabb.merge(m_aNodes[nRight].m_aabb);
        m_aNodes.push_back(std::move(node));
        return m_aNodes.size() - 1u;
    }

    // ── Traversal ──────────────────────────────────────────────────

    bool segment_intersects_node(
        std::size_t       nNodeIdx,
        const glm::vec3&  v3Origin,
        const glm::vec3&  v3Dir,
        const glm::vec3&  v3InvDir,
        float             fMaxT) const
    {
        const BvhNode& node = m_aNodes[nNodeIdx];

        if (!node.m_aabb.ray_intersect(v3Origin, v3InvDir, fMaxT))
            return false;

        if (node.m_bIsLeaf)
        {
            for (std::size_t nIdx : node.m_aPrimitives)
            {
                const auto hit = m_aTriangles[nIdx].ray_intersect(v3Origin, v3Dir);
                if (hit && std::get<0>(*hit) <= fMaxT)
                    return true;
            }
            return false;
        }

        return segment_intersects_node(node.m_nLeft,  v3Origin, v3Dir, v3InvDir, fMaxT)
            || segment_intersects_node(node.m_nRight, v3Origin, v3Dir, v3InvDir, fMaxT);
    }
};