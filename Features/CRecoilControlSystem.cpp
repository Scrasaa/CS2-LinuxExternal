#include "CRecoilControlSystem.h"

#include <cmath>

#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Config.h"
#include "Utils/Utils.h"

// ── CRecoilControlSystem.cpp ──────────────────────────────────────────────
//
//  CS2 recoil compensation — delta-punch approach.
//
//  View convention (Source 2 / CS2):
//    pitch  : negative = look up,   positive = look down
//    yaw    : negative = look left, positive = look right
//
//  m_aimPunchAngle is ADDED to the eye angles before rendering,
//  so the engine doubles it visually → we must multiply by 2.
//
//  To cancel a punch delta we apply the OPPOSITE sign to the mouse:
//    punch.x ↑  (+delta_pitch) → view kicked down  → mouse up   (−Y)
//    punch.x ↓  (-delta_pitch) → view kicked up    → mouse down (+Y)
//    punch.y →  (+delta_yaw)   → view kicked right → mouse left (-X)
//    punch.y ←  (-delta_yaw)   → view kicked left  → mouse right(+X)
//  ∴ both axes need negation.
//
//  Sensitivity formula (unchanged from CS:GO):
//    view_degrees = (mickeys × sens × 0.022)
//    mickeys      = degrees  / (sens × 0.022)
//
// ─────────────────────────────────────────────────────────────────────────

void CRCS::Run() noexcept
{
    if (!g_config.rcs.bEnable)
    {
        printf("disabled in config\n");
        return;
    }

    // ── Null-pawn guard ──────────────────────────────────────
    // If the cache hasn't resolved yet every ReadMem returns 0,
    // lifeState reads as 0 (alive), punch reads as {0,0} — the
    // function silently no-ops every frame.
    if (!g_EntityCache.m_local_pawn)
    {
        printf("local_pawn is null\n");
        Reset();
        return;
    }

    // ── Liveness ─────────────────────────────────────────────
    const uint8_t u8_life = R().ReadMem<uint8_t>(
        g_EntityCache.m_local_pawn +
        SCHEMA_OFFSET(C_BaseEntity, m_lifeState));

    printf("lifeState = %u\n", u8_life);

    if (u8_life != 0)   // 0 = LIFE_ALIVE in Source 2
    {
        Reset();
        return;
    }

    // ── Sensitivity guard ─────────────────────────────────────
    const float f_sens = GetSensitivity(g_offsets);
    printf("sensitivity = %.4f\n", f_sens);

    if (f_sens < 0.01f || !std::isfinite(f_sens))
    {
        printf("bad sensitivity, bailing\n");
        return;
    }

    const float f_micky_per_deg = 1.f / (f_sens * 0.022f);

    // ── Read punch ────────────────────────────────────────────
    const auto v_punch = R().ReadMem<ImVec2>(
        g_EntityCache.m_local_pawn +
        SCHEMA_OFFSET(C_CSPlayerPawn, m_pAimPunchServices));

    printf("punch = (%.4f, %.4f)\n", v_punch.x, v_punch.y);

    // ── Baseline ──────────────────────────────────────────────
    if (!m_b_initialized)
    {
        m_v_last_punch  = v_punch;
        m_b_initialized = true;
        printf("baseline set\n");
        return;
    }

    // ── Delta (CS2 view doubles the punch) ────────────────────
    const float f_dp = (v_punch.x - m_v_last_punch.x) * 2.f;  // pitch
    const float f_dy = (v_punch.y - m_v_last_punch.y) * 2.f;  // yaw
    m_v_last_punch = v_punch;

    printf("delta pitch = %.4f  yaw = %.4f\n", f_dp, f_dy);

    constexpr float k_dead_zone = 0.01f;
    if (std::abs(f_dp) < k_dead_zone && std::abs(f_dy) < k_dead_zone)
    {
        printf("below dead zone, skipping\n");
        return;
    }

    // ── Config scale sanity ───────────────────────────────────
    printf("scale x=%.2f y=%.2f\n",
        g_config.rcs.f_scale_x, g_config.rcs.f_scale_y);

    // ── Mouse delta ───────────────────────────────────────────
    // mouse.move already accumulates subpixels internally — pass
    // raw floats, do NOT accumulate here too.
    //
    // Sign: compensation must oppose the punch direction.
    //   +delta_pitch → view kicked down → mouse up   → -REL_Y
    //   +delta_yaw   → view kicked right → mouse left → -REL_X
    //
    // If RCS_DEBUG shows correct deltas but crosshair moves the
    // WRONG WAY, flip both signs here.
    const float f_mx = -f_dy * g_config.rcs.f_scale_x * f_micky_per_deg;
    const float f_my = -f_dp * g_config.rcs.f_scale_y * f_micky_per_deg;

    printf("sending mouse (%.3f, %.3f)\n", f_mx, f_my);

    Utils::mouse.move(f_mx, f_my);
}

void CRCS::Reset() noexcept
{
    m_v_last_punch  = {};
    m_b_initialized = false;
}