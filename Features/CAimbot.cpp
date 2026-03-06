//
// Created by scrasa on 04.03.26.
//

#include "CAimbot.h"

#include <cmath>
#include <numbers>

#include "CESP.h"
#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Config.h"
#include "Utils/Utils.h"
#include "Utils/BVH/map_manager.h"

// ── Focal length ──────────────────────────────────────────────────────────────
// CS2 is locked at 90° horizontal FOV (non-scoped).
// focal = (screen_w / 2) / tan(fov / 2)
// At 2560x1440 @ 90°  →  focal == 1280.0f exactly.
//
// Scoped overrides (if needed later):
//   AWP  :  8°  →  ~16380.0f
//   SSG  : 15°  →  ~ 8687.0f
//   Scout: 15°  →  ~ 8687.0f
constexpr float k_cs2_fov_deg = 90.0f;
constexpr float k_screen_w    = 2560.0f;
constexpr float k_screen_h    = 1440.0f;

float m_f_focal = (k_screen_w * 0.5f)
                / std::tan(k_cs2_fov_deg * (std::numbers::pi_v<float> / 180.0f) * 0.5f);

// ── Punch → screen-space pixel offset ────────────────────────────────────────
struct ScreenPunch
{
    float f_x;
    float f_y;
};

[[nodiscard]]
ScreenPunch punch_to_screen(float punch_yaw_deg, float punch_pitch_deg) noexcept
{
    constexpr float k_to_rad = std::numbers::pi_v<float> / 180.0f;
    return
    {
        .f_x = std::tan(punch_yaw_deg   * k_to_rad) * m_f_focal,
        .f_y = std::tan(punch_pitch_deg * k_to_rad) * m_f_focal,
    };
}

// ── Target selection ──────────────────────────────────────────────────────────
uintptr_t CAimbot::GetClosestToScreen(uintptr_t local_pawn)
{
    const auto local_team      = R().ReadMem<int32_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));
    float      f_closest_dist  = FLT_MAX;
    uintptr_t  target_pawn     = 0;

    for (const auto& pawn : g_EntityCache.get_pawns())
    {
        if (!is_valid_ptr(pawn))
            continue;

        if (R().ReadMem<uint8_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
            continue;

        if (!g_is_ffa && R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum)) == local_team)
            continue;

        const auto game_scene_node = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
        if (!is_valid_ptr(game_scene_node))
            continue;

        if (R().ReadMem<bool>(game_scene_node + SCHEMA_OFFSET(CGameSceneNode, m_bDormant)))
            continue;

        const auto head_pos = bone_position(game_scene_node, static_cast<uint64_t>(Bones::Head));
        const auto head_w2s = Utils::Math::WorldToScreen(head_pos);

        if (!head_w2s.has_value())
            continue;

        const auto screen_head_pos = head_w2s.value();
        const float f_dist = std::hypotf(
            screen_head_pos.x - k_screen_w * 0.5f,
            screen_head_pos.y - k_screen_h * 0.5f);

        if (f_dist > g_config.aimbot.fRadius)
            continue;

        if (f_dist < f_closest_dist)
        {
            f_closest_dist = f_dist;
            target_pawn    = pawn;
        }
    }

    return target_pawn;
}
// DO RCS Seperately?,

void normalize_angles(Utils::Math::Vector& a_angles)
{
    // Normalize yaw to [-180, 180]
    while (a_angles.y >  180.0f) a_angles.y -= 360.0f;
    while (a_angles.y < -180.0f) a_angles.y += 360.0f;

    // Clamp pitch to valid range
    a_angles.x = std::clamp(a_angles.x, -89.0f, 89.0f);

    // Roll unused in CS2
    a_angles.z = 0.0f;
}

std::pair<float, float> calc_angle(const Utils::Math::Vector& a_src, const Utils::Math::Vector& a_dst)
{
    const float f_dx = a_dst.x - a_src.x;
    const float f_dy = a_dst.y - a_src.y;
    const float f_dz = a_dst.z - a_src.z;

    const float f_yaw   =  std::atan2f(f_dy, f_dx)                     * (180.0f / std::numbers::pi_v<float>);
    const float f_pitch = -std::atan2f(f_dz, std::hypotf(f_dx, f_dy))  * (180.0f / std::numbers::pi_v<float>);

    Utils::Math::Vector angles { f_pitch, f_yaw, 0.0f };

    normalize_angles(angles);

    return { angles.x, angles.y };
}

// ── Main ──────────────────────────────────────────────────────────────────────
void CAimbot::Run()
{
    if (!g_config.aimbot.bEnable)
        return;

    if (!g_EntityCache.refresh())
        return;

    const auto local_pawn = g_EntityCache.resolve_entity_from_handle(
        R().ReadMem<uintptr_t>(g_EntityCache.m_p_localplayer_controller
            + SCHEMA_OFFSET(CBasePlayerController, m_hPawn)));

    if (!is_valid_ptr(local_pawn))
        return;

    if (R().ReadMem<uint8_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
        return;

    const auto target = GetClosestToScreen(local_pawn);
    if (!is_valid_ptr(target))
        return;

    const auto target_gsn = R().ReadMem<uintptr_t>(target + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
    if (!is_valid_ptr(target_gsn))
        return;

    const auto head_pos       = bone_position(target_gsn, static_cast<uint64_t>(Bones::Head));
    const auto local_gsn      = R().ReadMem<uintptr_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
    const auto local_head_pos = bone_position(local_gsn, static_cast<uint64_t>(Bones::Head));

    if (g_config.aimbot.bVisible)
    {
        if (!g_map_manager.is_visible(
        { local_head_pos.x, local_head_pos.y, local_head_pos.z },
        { head_pos.x,       head_pos.y,       head_pos.z       }))
            return;
    }

    const auto head_w2s = Utils::Math::WorldToScreen(head_pos);
    if (!head_w2s.has_value())
        return;

    const auto screen_head_pos = head_w2s.value();

    //const auto v_punch = R().ReadMem<ImVec2>(local_pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_flAimPunchAngle));


    // ── Smoothing value reference ─────────────────────────────────────────────
    // smoothing    behaviour                       detection risk
    // ─────────────────────────────────────────────────────────────────────────
    // 1.00f        instant snap to target          extreme
    // 0.30–0.50f   fast but smooth                 high
    // 0.12–0.20f   comfortable tracking            low
    // 0.08–0.12f   sweet spot                      very low
    // < 0.05f      sluggish, misses moving targets  very low
    //
    // k_inertia=0.62f compounds with smoothing — 0.10f feels like ~0.35f
    // ─────────────────────────────────────────────────────────────────────────
    Utils::mouse.aim_at(screen_head_pos.x, screen_head_pos.y, g_config.aimbot.fSmoothness);
}