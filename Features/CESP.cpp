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

enum EPlayerFlags : uint8_t
{
    FLAG_DEFUSING    = 1 << 0,
    FLAG_PLANTING    = 1 << 1,
    FLAG_SCOPED      = 1 << 2,
    FLAG_FLASHED     = 1 << 3,
    FLAG_HAS_C4      = 1 << 4,
    FLAG_HAS_DEFUSER = 1 << 5,
    FLAG_HAS_HELMET  = 1 << 6
};

struct PlayerInfo
{
    std::string szName{};
    int iHealth{};
    int iMaxHealth{};
    int iArmor{};
    uint8_t flags{};
    int iMoney{};
    std::string szWeaponName{}; // might need another struct ;(((
    int iMagCount{};
    int iAmmoCount{};
    int iMaxAmmoCount{};

    [[nodiscard]] inline bool HasFlag(const uint8_t f) const
    {
        return (flags & f) != 0;
    }

    inline void SetFlag(const uint8_t f)
    {
        flags |= f;
    }

    inline void ClearFlag(const uint8_t f)
    {
        flags &= ~f;
    }
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

    for (const auto& [controlller, pawn] : g_EntityCache.get_entity_pairs())
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

        if (auto pawn_dormant =  R().ReadMem<bool>(game_scene_node + SCHEMA_OFFSET(CGameSceneNode, m_bDormant)))
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
            DrawSkeleton(p_draw_list, bone_map);

        PlayerInfo player_info{};
        player_info.szName = R().ReadString(controlller + SCHEMA_OFFSET(CBasePlayerController, m_iszPlayerName));
        player_info.iHealth = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iHealth));
        player_info.iMaxHealth = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iMaxHealth));
        player_info.iArmor = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_ArmorValue));

        // Flags
        player_info.flags = 0;

        if (R().ReadMem<int8_t>(pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_bIsScoped)))
            player_info.SetFlag(FLAG_SCOPED);

        if (R().ReadMem<int8_t>(pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_bIsDefusing)))
            player_info.SetFlag(FLAG_DEFUSING);

        auto player_item_services = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pItemServices));

        if (is_valid_ptr(player_item_services))
        {
            if (R().ReadMem<int8_t>(player_item_services + SCHEMA_OFFSET(CCSPlayer_ItemServices, m_bHasDefuser)))
                player_info.SetFlag(FLAG_HAS_DEFUSER);

            if (R().ReadMem<int8_t>(player_item_services + SCHEMA_OFFSET(CCSPlayer_ItemServices, m_bHasHelmet)))
                player_info.SetFlag(FLAG_HAS_HELMET);
        }

        if (g_config.esp.player.bDraw2DBox)
            Draw2DBox(p_draw_list, bone_map,  player_info);
    }

    // For simplicity, I just do the other ImGui stuff here.
    if (g_config.visuals.bDrawFovCircle)
        DrawFOVIndicator(p_draw_list, local_pawn);

    DrawSpectatorList(p_draw_list, local_pawn);
}

void CESP::DrawSkeleton(
    ImDrawList*  p_draw_list,
    const std::unordered_map<Bones, Utils::Math::Vector>& bone_map,
    ImU32        color,
    float        thickness)
{
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

// Rewrite this, calculate min max, in above and pass it to diff funcs
void CESP::Draw2DBox(
    ImDrawList*                                        p_draw_list,
    const std::unordered_map<Bones, Utils::Math::Vector>& bone_map,
    const PlayerInfo&                                  player_info,
    ImU32                                              box_color,
    ImU32                                              text_color,
    float                                              thickness)
{
    float f_min_x = FLT_MAX,  f_min_y = FLT_MAX;
    float f_max_x = -FLT_MAX, f_max_y = -FLT_MAX; // FLT_MIN != -FLT_MAX

    bool b_any_valid = false;

    for (const Bones bone : k_needed_bones)
    {
        const auto it = bone_map.find(bone);
        if (it == bone_map.end())
            continue;

        const auto screen = Utils::Math::WorldToScreen(it->second);
        if (!screen.has_value())
            continue;

        const ImVec2 v_pos = screen.value();
        f_min_x = std::min(f_min_x, v_pos.x);
        f_min_y = std::min(f_min_y, v_pos.y);
        f_max_x = std::max(f_max_x, v_pos.x);
        f_max_y = std::max(f_max_y, v_pos.y);

        b_any_valid = true;
    }

    if (!b_any_valid)
        return;

    constexpr float k_padding = 6.f;
    f_min_x -= k_padding;
    f_min_y -= k_padding;
    f_max_x += k_padding;
    f_max_y += k_padding;

    p_draw_list->AddRect(
        ImVec2(f_min_x, f_min_y),
        ImVec2(f_max_x, f_max_y),
        box_color,
        0.f,
        ImDrawFlags_None,
        thickness);

    // --- Name label centered above the box ---
    const ImVec2 v_text_size = ImGui::CalcTextSize(player_info.szName.c_str());
    const float  f_text_x    = f_min_x + ((f_max_x - f_min_x) - v_text_size.x) * 0.5f;
    const float  f_text_y    = f_min_y - v_text_size.y - 3.f;

    p_draw_list->AddText(ImVec2(f_text_x + 1.f, f_text_y + 1.f), IM_COL32(0, 0, 0, 200), player_info.szName.c_str());
    p_draw_list->AddText(ImVec2(f_text_x,        f_text_y),       text_color,              player_info.szName.c_str());

    // --- Health bar ---
    const float f_health_frac = std::clamp(
        static_cast<float>(player_info.iHealth) / static_cast<float>(player_info.iMaxHealth),
        0.f, 1.f);

    constexpr float k_bar_width = 4.f;
    constexpr float k_bar_gap   = 8.f;

    const float f_bar_x = f_min_x - k_bar_gap;
    const float f_bar_y = f_min_y;
    const float f_bar_h = f_max_y - f_min_y;

    // Background
    p_draw_list->AddRectFilled(
        ImVec2(f_bar_x,               f_bar_y),
        ImVec2(f_bar_x + k_bar_width, f_bar_y + f_bar_h),
        IM_COL32(0, 0, 0, 200));

    // Green → red based on fraction
    const ImU32 u_health_color = IM_COL32(
        static_cast<int>((1.f - f_health_frac) * 255.f),
        static_cast<int>(f_health_frac         * 255.f),
        0,
        255);

    // Filled portion bottom-up
    const float f_filled_h  = f_bar_h * f_health_frac;
    const float f_filled_y0 = f_bar_y + (f_bar_h - f_filled_h);

    p_draw_list->AddRectFilled(
        ImVec2(f_bar_x,               f_filled_y0),
        ImVec2(f_bar_x + k_bar_width, f_bar_y + f_bar_h),
        u_health_color);

    // --- Health text ---
    char sz_health_text[8];
    snprintf(sz_health_text, sizeof(sz_health_text), "%d", player_info.iHealth);

    const ImVec2 v_hp_text_size = ImGui::CalcTextSize(sz_health_text);

    const float f_hp_text_x = f_bar_x - v_hp_text_size.x - 4.f; //+ (k_bar_width * 0.5f) - (v_hp_text_size.x * 0.5f);
    const float f_hp_text_y = f_filled_y0 - (v_hp_text_size.y / 2);

    p_draw_list->AddText(
        ImVec2(f_hp_text_x + 1.f, f_hp_text_y + 1.f),
        IM_COL32(0, 0, 0, 200),
        sz_health_text);

    p_draw_list->AddText(
        ImVec2(f_hp_text_x, f_hp_text_y),
        IM_COL32(255, 255, 255, 255),
        sz_health_text);
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

constexpr ImU32 outline_color = IM_COL32(0, 0, 0, 255);
constexpr float offset = 1.0f;
const char* header_text = "Spectator List:";
constexpr ImVec2 header_pos(20, 450.f - 15);

void CESP::DrawSpectatorList(ImDrawList* p_draw_list, uintptr_t local_pawn)
{
    auto y = 0.f;

    p_draw_list->AddText(ImVec2(header_pos.x - offset, header_pos.y - offset), outline_color, header_text);
    p_draw_list->AddText(ImVec2(header_pos.x + offset, header_pos.y - offset), outline_color, header_text);
    p_draw_list->AddText(ImVec2(header_pos.x - offset, header_pos.y + offset), outline_color, header_text);
    p_draw_list->AddText(ImVec2(header_pos.x + offset, header_pos.y + offset), outline_color, header_text);

    p_draw_list->AddText(header_pos, IM_COL32(255, 255, 255, 255), header_text);

    std::set<std::string> spectator_set;

    for (const auto& pawn : g_EntityCache.get_pawns())
    {
        if (!is_valid_ptr(pawn) || pawn == local_pawn)
            continue;

        auto observer_service = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pObserverServices));
        if (!is_valid_ptr(observer_service))
            continue;

        auto observer_target = R().ReadMem<uintptr_t>(observer_service + SCHEMA_OFFSET(CPlayer_ObserverServices, m_hObserverTarget));
        auto target_pawn = g_EntityCache.resolve_entity_from_handle(observer_target);
        if (!is_valid_ptr(target_pawn) || target_pawn != local_pawn)
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

        spectator_set.insert(name + " (" + mode_str + ")");
    }

    for (const auto& entry : spectator_set)
    {
        ImVec2 pos(20, 450.f + y);
        const char* text = entry.c_str();

        p_draw_list->AddText(ImVec2(pos.x - offset, pos.y - offset), outline_color, text);
        p_draw_list->AddText(ImVec2(pos.x + offset, pos.y - offset), outline_color, text);
        p_draw_list->AddText(ImVec2(pos.x - offset, pos.y + offset), outline_color, text);
        p_draw_list->AddText(ImVec2(pos.x + offset, pos.y + offset), outline_color, text);

        // Draw main text on top
        p_draw_list->AddText(pos, IM_COL32(255, 0, 0, 255), text);
        y += 15.f;
    }
}