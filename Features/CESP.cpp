//
// Created by scrasa on 04.03.26.
//

#include "CESP.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

#include "globals.h"
#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Utils.h"

#include "../Thirdparty/ImGUI/imgui.h"
#include "Utils/Config.h"
#include "Utils/BVH/map_manager.h"

#define DEG2RAD(x) ((x) * (3.14159265358979323846 / 180.0))

inline constexpr std::array<std::pair<Bones, Bones>, 18> BoneConnections
{{
    { Bones::Hip,          Bones::Spine1       },
    { Bones::Spine1,       Bones::Spine2       },
    { Bones::Spine2,       Bones::Spine3       },
    { Bones::Spine3,       Bones::Spine4       },
    { Bones::Spine4,       Bones::Neck         },
    { Bones::Neck,         Bones::Head         },
    { Bones::Neck,         Bones::LeftShoulder  },
    { Bones::LeftShoulder, Bones::LeftElbow     },
    { Bones::LeftElbow,    Bones::LeftHand      },
    { Bones::Neck,         Bones::RightShoulder },
    { Bones::RightShoulder,Bones::RightElbow    },
    { Bones::RightElbow,   Bones::RightHand     },
    { Bones::Hip,          Bones::LeftHip       },
    { Bones::LeftHip,      Bones::LeftKnee      },
    { Bones::LeftKnee,     Bones::LeftFoot      },
    { Bones::Hip,          Bones::RightHip      },
    { Bones::RightHip,     Bones::RightKnee     },
    { Bones::RightKnee,    Bones::RightFoot     },
}};

inline constexpr std::array<Bones, 19> k_needed_bones =
{
    Bones::Hip,
    Bones::Spine1, Bones::Spine2, Bones::Spine3, Bones::Spine4,
    Bones::Neck,   Bones::Head,
    Bones::LeftShoulder,  Bones::LeftElbow,  Bones::LeftHand,
    Bones::RightShoulder, Bones::RightElbow, Bones::RightHand,
    Bones::LeftHip,  Bones::LeftKnee,  Bones::LeftFoot,
    Bones::RightHip, Bones::RightKnee, Bones::RightFoot,
};

struct Bone
{
    Utils::Math::Vector position; // 12 bytes
    std::byte padding[20];        // rest of 32-byte struct
};

Utils::Math::Vector bone_position(const uintptr_t game_scene_node, const uint64_t bone_index)
{
    const auto bone_data = R().ReadMem<uintptr_t>(
     game_scene_node
     + SCHEMA_OFFSET(CSkeletonInstance, m_modelState)
     + SCHEMA_OFFSET(CBodyComponentSkeletonInstance, m_skeletonInstance));

    if (bone_data == 0)
        return {0.f, 0.f, 0.f};

    Bone bone_struct{};
    if (!R().ReadRaw(bone_data + bone_index * sizeof(Bone), &bone_struct, sizeof(Bone)))
        return {0.f, 0.f, 0.f};

    return bone_struct.position;
}

std::unordered_map<Bones, Utils::Math::Vector> all_bones(const uintptr_t game_scene_node)
{
    std::unordered_map<Bones, Utils::Math::Vector> bones;

    // Single read — flat offset sum, exactly like the Rust
    const auto bone_data = R().ReadMem<uintptr_t>(
        game_scene_node
        + SCHEMA_OFFSET(CSkeletonInstance, m_modelState)
        + SCHEMA_OFFSET(CBodyComponentSkeletonInstance, m_skeletonInstance));

    if (bone_data == 0)
        return bones;

    constexpr size_t k_max_bone = 28;
    std::array<Bone, k_max_bone> buffer{};

    if (!R().ReadRaw(bone_data, buffer.data(), sizeof(buffer)))
        return bones;

    for (const Bones bone : k_needed_bones)
    {
        const auto index = static_cast<uint64_t>(bone);
        bones.emplace(bone, buffer[index].position);
    }

    return bones;
}

void CESP::Run()
{
    if (!g_config.esp.player.bEnable)
        return;

    auto* p_draw_list = ImGui::GetForegroundDrawList();
    if (!p_draw_list)
        return;

    if (!g_EntityCache.refresh())
        return;

    auto local_pawn = g_EntityCache.resolve_entity_from_handle(R().ReadMem<uintptr_t>(g_EntityCache.m_p_localplayer_controller + SCHEMA_OFFSET(CBasePlayerController, m_hPawn)));

    if (!is_valid_ptr(local_pawn))
        return;

    auto local_team = R().ReadMem<int32_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));

    for (const auto& [contoller, pawn] : g_EntityCache.get_entity_pairs())
    {
        if (!is_valid_ptr(pawn))
            continue;

        auto pawn_lifestate = R().ReadMem<uint8_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState));

        if (pawn_lifestate != 0)
            continue;

        auto pawn_team = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));

        if (!g_is_ffa)
        {
            if (!g_config.esp.player.bTeam && pawn_team == local_team)
                continue;
        }

        const auto game_scene_node = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
        if (!is_valid_ptr(game_scene_node))
            continue;

        auto pawn_dormant =  R().ReadMem<bool>(game_scene_node + SCHEMA_OFFSET(CGameSceneNode, m_bDormant));

        if (pawn_dormant)
            continue;

        const auto bone_map = all_bones(game_scene_node);

        if (bone_map.empty())
            continue;

        const auto it_head = bone_map.find(Bones::Head);
        if (it_head == bone_map.end())
            continue;

        const auto& head_pos = it_head->second;

        const auto& local_head_pos = bone_position( R().ReadMem<uintptr_t>(local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode)), static_cast<uint64_t>(Bones::Head));

        if (g_config.esp.player.bVisible)
        {
            if (!g_MapManager.is_visible(
            { local_head_pos.x, local_head_pos.y, local_head_pos.z },
            { head_pos.x,       head_pos.y,       head_pos.z       }))
                continue;
        }

        if (g_config.esp.player.bSkeleton)
            DrawSkeleton(p_draw_list, game_scene_node);

        if (g_config.esp.player.bDraw2DBox)
            Draw2DBox(p_draw_list, bone_map, R().ReadString(contoller + SCHEMA_OFFSET(CBasePlayerController, m_iszPlayerName)) );
    }

    // For simplicity, I just do the other ImGui stuff here.
    if (g_config.visuals.bDrawFovCircle)
        DrawFOVIndicator(p_draw_list, local_pawn);

    DrawSpectatorList(p_draw_list, local_pawn);
}

void CESP::DrawSkeleton(
    ImDrawList*  p_draw_list,
    uintptr_t    game_scene_node,
    ImU32        color,
    float        thickness)
{
    const auto bone_map = all_bones(game_scene_node);

    if (bone_map.empty())
        return;

    for (const auto& [bone_a, bone_b] : BoneConnections)
    {
        const auto it_a = bone_map.find(bone_a);
        const auto it_b = bone_map.find(bone_b);

        if (it_a == bone_map.end() || it_b == bone_map.end())
            continue;

        const auto screen_a = Utils::Math::WorldToScreen(it_a->second);
        const auto screen_b = Utils::Math::WorldToScreen(it_b->second);

        if (!screen_a.has_value() || !screen_b.has_value())
            continue;

        p_draw_list->AddLine(screen_a.value(), screen_b.value(), color, thickness);
    }
}

void CESP::Draw2DBox(
    ImDrawList*                                        p_draw_list,
    const std::unordered_map<Bones, Utils::Math::Vector>& bone_map,
    const std::string&                                 name,
    ImU32                                              box_color,
    ImU32                                              text_color,
    float                                              thickness)
{
    float min_x = FLT_MAX, min_y = FLT_MAX;
    float max_x = FLT_MIN, max_y = FLT_MIN;

    bool any_valid = false;

    for (const Bones bone : k_needed_bones)
    {
        const auto it = bone_map.find(bone);
        if (it == bone_map.end())
            continue;

        const auto screen = Utils::Math::WorldToScreen(it->second);
        if (!screen.has_value())
            continue;

        const ImVec2 pos = screen.value();
        min_x = std::min(min_x, pos.x);
        min_y = std::min(min_y, pos.y);
        max_x = std::max(max_x, pos.x);
        max_y = std::max(max_y, pos.y);

        any_valid = true;
    }

    if (!any_valid)
        return;

    constexpr float k_padding = 6.f;
    min_x -= k_padding;
    min_y -= k_padding;
    max_x += k_padding;
    max_y += k_padding;

    p_draw_list->AddRect(
        ImVec2(min_x, min_y),
        ImVec2(max_x, max_y),
        box_color,
        0.f,
        ImDrawFlags_None,
        thickness);

    // Name label centered above the box
    const ImVec2 text_size   = ImGui::CalcTextSize(name.c_str());
    const float  text_x      = min_x + ((max_x - min_x) - text_size.x) * 0.5f;
    const float  text_y      = min_y - text_size.y - 3.f;

    // Drop shadow for readability
    p_draw_list->AddText(ImVec2(text_x + 1.f, text_y + 1.f), IM_COL32(0, 0, 0, 200), name.c_str());
    p_draw_list->AddText(ImVec2(text_x,        text_y),       text_color,              name.c_str());
}

void CESP::DrawFOVIndicator(ImDrawList *p_draw_list, uintptr_t local_pawn)
{
    // Draw the circle
    p_draw_list->AddCircle
    (
        {(g_screen_w / 2), (g_screen_h / 2)},
        g_config.aimbot.fRadius,
        IM_COL32(255, 0, 0, 200), // Red with transparency
        64,
        1.5f
    );
}

void CESP::DrawSpectatorList(ImDrawList *p_draw_list, uintptr_t local_pawn)
{
    auto y = 0.f;
    p_draw_list->AddText(ImVec2(20, 450.f - 15),IM_COL32(255, 255,255,255),"Spectator List:");
    std::set<std::string> spectatorSet;

    for (const auto& pawn : g_EntityCache.get_pawns())
    {
        if (!is_valid_ptr(pawn) || pawn == local_pawn)
            continue;

        auto observer_service = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pObserverServices));
        if (!is_valid_ptr(observer_service))
            continue;

        auto observer_target = R().ReadMem<uintptr_t>(observer_service + SCHEMA_OFFSET(CPlayer_ObserverServices, m_hObserverTarget));
        auto target_pawn = g_EntityCache.resolve_entity_from_handle(observer_target);
        if (!is_valid_ptr(target_pawn))
            continue;

        auto target_controller = g_EntityCache.resolve_entity_from_handle(
            R().ReadMem<uintptr_t>(target_pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_hController))
        );
        if (!is_valid_ptr(target_controller))
            continue;

        auto name = R().ReadString(target_controller + SCHEMA_OFFSET(CBasePlayerController, m_iszPlayerName));
        if (!name.empty())
            spectatorSet.insert(name);
    }

    // Draw
    for (const auto& playerName : spectatorSet)
    {
        p_draw_list->AddText(ImVec2(20, 450.f + y), IM_COL32(255, 0, 0, 255), playerName.c_str());
        y += 15.f;
    }
}