//
// Created by scrasa on 05.03.26.
//

#include "CTriggerbot.h"

#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Config.h"
#include "Utils/Utils.h"

int RandomReactionTime()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(g_config.triggerbot.iMinReaction, g_config.triggerbot.iMaxReaction);

    return dist(gen);
}

void CTriggerbot::Run()
{
    if (!g_config.triggerbot.bEnable)
        return;

    const auto  l_local_pawn_handle = R().ReadMem<uint32_t>(
        g_EntityCache.m_p_localplayer_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn));

    const uintptr_t lp_local_pawn = g_EntityCache.resolve_entity_from_handle(l_local_pawn_handle);
    if (!is_valid_ptr(lp_local_pawn))
        return;

    if (R().ReadMem<uint8_t>(lp_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
        return;

    const auto l_local_team = R().ReadMem<int32_t>(lp_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));
    const auto l_ent_idx    = R().ReadMem<int32_t>(lp_local_pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_iIDEntIndex));

    if (l_ent_idx < 1)
    {
        m_acquire_time = std::nullopt; // lost target, reset
        return;
    }

    const uintptr_t lp_target_pawn = g_EntityCache.read_entity_at_index(l_ent_idx);
    if (!is_valid_ptr(lp_target_pawn))
    {
        m_acquire_time = std::nullopt;
        return;
    }

    if (!g_is_ffa && R().ReadMem<int32_t>(lp_target_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum)) == l_local_team)
    {
        m_acquire_time = std::nullopt;
        return;
    }

    // First frame we see a valid target — stamp it
    if (!m_acquire_time.has_value())
    {
        m_acquire_time = std::chrono::steady_clock::now();
        m_reaction_ms  = RandomReactionTime();
        return;
    }

    const auto l_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - *m_acquire_time).count();

    if (l_elapsed_ms < m_reaction_ms)
        return;

    Utils::mouse.mouse1();
    m_acquire_time = std::nullopt; // reset so it doesn't spam
}