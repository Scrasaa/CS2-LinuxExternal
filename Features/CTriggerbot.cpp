//
// Created by scrasa on 05.03.26.
//

#include "CTriggerbot.h"

#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Utils.h"

void CTriggerbot::Run()
{
    const uint32_t  l_local_pawn_handle = R().ReadMem<uint32_t>(
        g_EntityCache.m_p_localplayer_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn));

    const uintptr_t lp_local_pawn = g_EntityCache.resolve_entity_from_handle(l_local_pawn_handle);
    if (!is_valid_ptr(lp_local_pawn))
        return;

    if (R().ReadMem<uint8_t>(lp_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
        return;

    const int32_t l_local_team = R().ReadMem<int32_t>(lp_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));
    const int32_t l_ent_idx    = R().ReadMem<int32_t>(lp_local_pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_iIDEntIndex));

    if (l_ent_idx < 1)
        return;

    // m_iIDEntIndex is the pawn index — read it directly, no controller hop needed
    const uintptr_t lp_target_pawn = g_EntityCache.read_entity_at_index(l_ent_idx);
    if (!is_valid_ptr(lp_target_pawn))
        return;

    if (R().ReadMem<int32_t>(lp_target_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum)) == l_local_team)
        return;

    // Config later, we could use curtime and a float, scope if sniper all var..
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Utils::mouse.mouse1();
}