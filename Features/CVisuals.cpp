//
// Created by scrasa on 19.04.26.
//

#include "CVisuals.h"

#include <set>

#include "globals.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Config.h"
#include "Utils/Overlay.h"
#include "Utils/Utils.h"

// Move to Visuals
void CVisuals::DrawFOVIndicator()
{
    // Draw the circle
    Overlay::draw_list->AddCircle
    (
        {(g_screen_w / 2), (g_screen_h / 2)},
        g_config.aimbot.fRadius,
        IM_COL32(255, 0, 0, 200), // Red with transparency
        64,
        1.5f
    );
}

constexpr ImU32 outline_color = IM_COL32(0, 0, 0, 255);
constexpr float offset = 1.0f;
const char* header_text = "Spectator List:";
constexpr ImVec2 header_pos(20, 450.f - 15);

void CVisuals::DrawSpectatorList()
{
    auto y = 0.f;

    Overlay::draw_list->AddText(ImVec2(header_pos.x - offset, header_pos.y - offset), outline_color, header_text);
    Overlay::draw_list->AddText(ImVec2(header_pos.x + offset, header_pos.y - offset), outline_color, header_text);
    Overlay::draw_list->AddText(ImVec2(header_pos.x - offset, header_pos.y + offset), outline_color, header_text);
    Overlay::draw_list->AddText(ImVec2(header_pos.x + offset, header_pos.y + offset), outline_color, header_text);

    Overlay::draw_list->AddText(header_pos, IM_COL32(255, 255, 255, 255), header_text);

    std::set<std::string> spectator_set;

    for (const auto& pawn : g_EntityCache.get_pawns())
    {
        if (!is_valid_ptr(pawn) || pawn == g_EntityCache.m_local_pawn)
            continue;

        auto observer_service = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pObserverServices));
        if (!is_valid_ptr(observer_service))
            continue;

        auto observer_target = R().ReadMem<uintptr_t>(observer_service + SCHEMA_OFFSET(CPlayer_ObserverServices, m_hObserverTarget));
        auto target_pawn = g_EntityCache.resolve_entity_from_handle(observer_target);
        if (!is_valid_ptr(target_pawn) || target_pawn != g_EntityCache.m_local_pawn)
            continue;

        // Resolve the SPECTATOR'S controller (pawn, not target_pawn)
        auto spectator_controller = g_EntityCache.resolve_entity_from_handle(
            R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_hController))
        );
        if (!is_valid_ptr(spectator_controller))
            continue;

        auto name = R().ReadString(spectator_controller + SCHEMA_OFFSET(CBasePlayerController, m_iszPlayerName));
        if (name.empty())
            continue;

        int observer_mode = R().ReadMem<int>(observer_service + SCHEMA_OFFSET(CPlayer_ObserverServices, m_iObserverMode));
        std::string mode_str = (observer_mode == 2) ? "first-person" : "third-person";

        spectator_set.insert(name + " (" + mode_str + ")"); // clang tidy
    }

    for (const auto& entry : spectator_set)
    {
        ImVec2 pos(20, 450.f + y);
        const char* text = entry.c_str();

        Overlay::draw_list->AddText(ImVec2(pos.x - offset, pos.y - offset), outline_color, text);
        Overlay::draw_list->AddText(ImVec2(pos.x + offset, pos.y - offset), outline_color, text);
        Overlay::draw_list->AddText(ImVec2(pos.x - offset, pos.y + offset), outline_color, text);
        Overlay::draw_list->AddText(ImVec2(pos.x + offset, pos.y + offset), outline_color, text);

        // Draw main text on top
        Overlay::draw_list->AddText(pos, IM_COL32(255, 0, 0, 255), text);
        y += 15.f;
    }
}