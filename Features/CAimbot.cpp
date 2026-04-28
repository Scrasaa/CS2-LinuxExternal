#include "CAimbot.h"

#include <cmath>
#include <numbers>
#include <optional>

#include "CESP.h"
#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Config.h"
#include "Utils/Utils.h"
#include "Utils/BVH/map_manager.h"

// ── Focal length ──────────────────────────────────────────────
//
//  CS2 is locked at 90° horizontal FOV (non-scoped).
//  focal = (screen_w / 2) / tan(fov / 2)
//  At 2560×1440 @ 90°  →  focal == 1280.0f exactly.
//
//  Scoped overrides (if needed later):
//    AWP  :  8°  →  ~16380.0f
//    SSG  : 15°  →  ~ 8687.0f
//    Scout: 15°  →  ~ 8687.0f

constexpr float k_fov_deg  = 90.0f;
constexpr float k_screen_w = 2560.0f;
constexpr float k_screen_h = 1440.0f;
constexpr float k_to_rad   = std::numbers::pi_v<float> / 180.0f;

const float k_focal = (k_screen_w * 0.5f)
                    / std::tan(k_fov_deg * k_to_rad * 0.5f);

// ── Punch → screen-space pixel offset ────────────────────────

struct ScreenPunch
{
    float f_x;
    float f_y;
};

[[nodiscard]]
ScreenPunch punch_to_screen(float punch_yaw_deg, float punch_pitch_deg) noexcept
{
    return
    {
        .f_x = std::tan(punch_yaw_deg   * k_to_rad) * k_focal,
        .f_y = std::tan(punch_pitch_deg * k_to_rad) * k_focal,
    };
}

// ── Angle helpers ─────────────────────────────────────────────

static void normalize_angles(Utils::Math::Vector& a_angles) noexcept
{
    while (a_angles.y >  180.0f) a_angles.y -= 360.0f;
    while (a_angles.y < -180.0f) a_angles.y += 360.0f;
    a_angles.x = std::clamp(a_angles.x, -89.0f, 89.0f);
    a_angles.z = 0.0f;
}

static std::pair<float, float> calc_angle(
    const Utils::Math::Vector& a_src,
    const Utils::Math::Vector& a_dst)
{
    const float f_dx = a_dst.x - a_src.x;
    const float f_dy = a_dst.y - a_src.y;
    const float f_dz = a_dst.z - a_src.z;

    const float f_yaw   =  std::atan2f(f_dy, f_dx)                    * (180.0f / std::numbers::pi_v<float>);
    const float f_pitch = -std::atan2f(f_dz, std::hypotf(f_dx, f_dy)) * (180.0f / std::numbers::pi_v<float>);

    Utils::Math::Vector angles { f_pitch, f_yaw, 0.0f };
    normalize_angles(angles);

    return { angles.x, angles.y };
}

// ── AimTarget ─────────────────────────────────────────────────
//
//  Bundles the resolved data for the best candidate so Run() can
//  consume it without re-reading bones or re-projecting to screen.

struct AimTarget
{
    const CEntityCache::CachedPlayer* lp_cached    = nullptr;
    Utils::Math::Vector               v_head_world {};   // for visibility ray
    ImVec2                            v_head_screen {};   // for mouse aim
};

// ── Target selection ──────────────────────────────────────────
//
//  Iterates m_cached_players — already filtered by refresh() for
//  alive + non-dormant.  Zero game-state RPM calls here;
//  only bone reads (one bulk RPM per candidate).

static std::optional<AimTarget> get_closest_to_screen()
{
    float      f_closest_dist = FLT_MAX;
    AimTarget  best;

    for (const auto& cached : g_EntityCache.get_cached_players())
    {
        if (!g_is_ffa && cached.info.iTeamNum == g_EntityCache.m_local_team)
            continue;

        const auto v_head = bone_position(
            cached.p_game_scene, static_cast<uint64_t>(Bones::Head));

        const auto head_w2s = Utils::Math::WorldToScreen(v_head);
        if (!head_w2s.has_value())
            continue;

        const ImVec2& screen = head_w2s.value();
        const float f_dist = std::hypotf(
            screen.x - k_screen_w * 0.5f,
            screen.y - k_screen_h * 0.5f);

        if (f_dist > g_config.aimbot.fRadius || f_dist >= f_closest_dist)
            continue;

        f_closest_dist     = f_dist;
        best.lp_cached     = &cached;
        best.v_head_world  = v_head;
        best.v_head_screen = screen;
    }

    if (!best.lp_cached)
        return std::nullopt;

    return best;
}

// ── CAimbot::Run ──────────────────────────────────────────────
//
//  Game-state reads are limited to:
//    – local lifeState        (1 RPM — local player, not in cache)
//    – local scene node       (1 RPM — only when bVisible is set)
//    – local head bone        (1 RPM — only when bVisible is set)
//  Target data comes entirely from the pre-populated cache.

void CAimbot::Run()
{
    if (!g_config.aimbot.bEnable)
        return;

    if (R().ReadMem<uint8_t>(g_EntityCache.m_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
        return;

    const auto target = get_closest_to_screen();
    if (!target.has_value())
        return;

    if (g_config.aimbot.bVisible)
    {
        const auto local_gsn = R().ReadMem<uintptr_t>(
            g_EntityCache.m_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));

        if (!is_valid_ptr(local_gsn))
            return;

        const auto local_head = bone_position(local_gsn, static_cast<uint64_t>(Bones::Head));
        const auto& h         = target->v_head_world;

        if (!g_MapManager.is_visible(
                { local_head.x, local_head.y, local_head.z },
                { h.x,          h.y,          h.z          }))
            return;
    }

    // ── Smoothing reference ───────────────────────────────────
    //
    //  smoothing    behaviour                        detection risk
    //  ──────────────────────────────────────────────────────────
    //  1.00f        instant snap to target           extreme
    //  0.30–0.50f   fast but smooth                  high
    //  0.12–0.20f   comfortable tracking             low
    //  0.08–0.12f   sweet spot                       very low
    //  < 0.05f      sluggish, misses moving targets   very low
    //
    //  k_inertia=0.62f compounds with smoothing — 0.10f feels like ~0.35f

    Utils::mouse.aim_at(
        target->v_head_screen.x,
        target->v_head_screen.y,
        g_config.aimbot.fSmoothness);
}