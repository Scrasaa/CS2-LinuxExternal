//
// Created by scrasa on 04.03.26.
//

#include "CAimbot.h"

#include <cmath>

#include "CESP.h"
#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Utils.h"
#include "Utils/BVH/map_manager.h"

uintptr_t CAimbot::GetClosestToScreen(uintptr_t local_pawn)
{
    auto local_team = R().ReadMem<int32_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));
    float fClosestDistance = FLT_MAX;
    uintptr_t target_pawn = 0;
    float fRadius = 100;

    for (const auto& pawn : g_EntityCache.get_pawns())
    {
        if (!is_valid_ptr(pawn))
            continue;

        auto pawn_lifestate = R().ReadMem<uint8_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState));

        if (pawn_lifestate != 0)
            continue;

        auto pawn_team = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));

        if (pawn_team == local_team)
            continue;

        const auto game_scene_node = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
        if (!is_valid_ptr(game_scene_node))
            continue;

        auto pawn_dormant =  R().ReadMem<bool>(game_scene_node + SCHEMA_OFFSET(CGameSceneNode, m_bDormant));

        if (pawn_dormant)
            continue;

        const auto head_pos = bone_position(game_scene_node, static_cast<uint64_t>(Bones::Head));

        const auto head_w2s = Utils::Math::WorldToScreen(head_pos, 2560, 1440);

        if (head_w2s.has_value())
        {
            const auto screen_head_pos = head_w2s.value();

            float fDistance = std::hypotf(screen_head_pos.x - 2560 / 2, screen_head_pos.y - 1440 / 2);

            if (fDistance > fRadius)
                continue;

            if (fDistance < fClosestDistance)
            {
                fClosestDistance = fDistance;
                target_pawn = pawn;
            }
        }
    }
    return target_pawn;
}

void CAimbot::Run()
{
    if (!g_EntityCache.refresh())
        return;

    auto local_pawn = g_EntityCache.resolve_entity_from_handle(R().ReadMem<uintptr_t>(g_EntityCache.m_p_localplayer_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn)));

    if (!is_valid_ptr(local_pawn))
        return;

    auto local_lifestate = R().ReadMem<uint8_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState));

    if (local_lifestate != 0)
        return;

    const auto target = GetClosestToScreen(local_pawn);

    if (!is_valid_ptr(target))
        return;

    const auto game_scene_node = R().ReadMem<uintptr_t>(target + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
    if (!is_valid_ptr(game_scene_node))
        return;

    const auto head_pos = bone_position(game_scene_node, static_cast<uint64_t>(Bones::Head));

    auto local_player_head = bone_position(R().ReadMem<uintptr_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode)),
        static_cast<uint64_t>(Bones::Head));

    if (!g_map_manager.is_visible({local_player_head.x, local_player_head.y, local_player_head.z}, {head_pos.x, head_pos.y, head_pos.z}))
        return;

    const auto head_w2s = Utils::Math::WorldToScreen(head_pos, 2560, 1440);

    // ── Smoothing value reference ─────────────────────────────────────────────────
    // aim_at(target_x, target_y, smoothing)
    //
    // smoothing    behaviour                       detection risk
    // ─────────────────────────────────────────────────────────────
    // 1.00f        instant snap to target          extreme  — single-frame velocity spike
    // 0.30f–0.50f  fast but smooth                 high     — suspiciously rapid acquisition
    // 0.12f–0.20f  comfortable tracking            low      — resembles high-sens player
    // 0.08f–0.12f  ✅ sweet spot                   very low — natural deceleration curve
    // < 0.05f      sluggish, misses moving targets  very low — poor aim tradeoff
    //
    // note: k_inertia=0.62f compounds with smoothing, so 0.10f feels like ~0.35f in practice
    // note: dead-frame skip adds irregularity, allowing slightly higher smoothing safely
    //
    // recommended: aim_at(target_x, target_y, 0.10f);  // general use
    //              aim_at(target_x, target_y, 0.14f);  // tactical / slow-paced
    //              aim_at(target_x, target_y, 0.08f);  // arena / fast-moving targets
    // ─────────────────────────────────────────────────────────────────────────────

    if (head_w2s.has_value())
    {
        const auto screen_head_pos = head_w2s.value();
        Utils::mouse.aim_at(screen_head_pos.x,screen_head_pos.y, 0.12f);
    }
}
