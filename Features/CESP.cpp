#include "CESP.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "globals.h"
#include "SDK/Helper/CEntityCache.h"
#include "SDK/Helper/CSchemaManager.h"
#include "Utils/Utils.h"
#include "Utils/Config.h"
#include "Utils/Overlay.h"
#include "Utils/BVH/map_manager.h"
#include "../Thirdparty/ImGUI/imgui.h"
#include "SDK/WeaponType.h"

inline constexpr std::array<std::pair<Bones, Bones>, 18> BoneConnections
{{
    { Bones::Hip,           Bones::Spine1        },
    { Bones::Spine1,        Bones::Spine2        },
    { Bones::Spine2,        Bones::Spine3        },
    { Bones::Spine3,        Bones::Spine4        },
    { Bones::Spine4,        Bones::Neck          },
    { Bones::Neck,          Bones::Head          },
    { Bones::Neck,          Bones::LeftShoulder  },
    { Bones::LeftShoulder,  Bones::LeftElbow     },
    { Bones::LeftElbow,     Bones::LeftHand      },
    { Bones::Neck,          Bones::RightShoulder },
    { Bones::RightShoulder, Bones::RightElbow    },
    { Bones::RightElbow,    Bones::RightHand     },
    { Bones::Hip,           Bones::LeftHip       },
    { Bones::LeftHip,       Bones::LeftKnee      },
    { Bones::LeftKnee,      Bones::LeftFoot      },
    { Bones::Hip,           Bones::RightHip      },
    { Bones::RightHip,      Bones::RightKnee     },
    { Bones::RightKnee,     Bones::RightFoot     },
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
    Utils::Math::Vector position;
    std::byte padding[20];
};

Utils::Math::Vector bone_position(const uintptr_t game_scene_node, const uint64_t bone_index)
{
    const auto bone_data = R().ReadMem<uintptr_t>(
        game_scene_node
        + SCHEMA_OFFSET(CSkeletonInstance, m_modelState)
        + SCHEMA_OFFSET(CBodyComponentSkeletonInstance, m_skeletonInstance));

    if (bone_data == 0)
        return { 0.f, 0.f, 0.f };

    Bone bone_struct{};
    if (!R().ReadRaw(bone_data + bone_index * sizeof(Bone), &bone_struct, sizeof(Bone)))
        return { 0.f, 0.f, 0.f };

    return bone_struct.position;
}

static std::unordered_map<Bones, Utils::Math::Vector> all_bones(const uintptr_t game_scene_node)
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
        bones.emplace(bone, buffer[static_cast<uint64_t>(bone)].position);

    return bones;
}

static std::optional<ScreenBounds> compute_screen_bounds(const std::unordered_map<Bones, Utils::Math::Vector>& bone_map)
{
    float min_x =  FLT_MAX, min_y =  FLT_MAX;
    float max_x = -FLT_MAX, max_y = -FLT_MAX;
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
        return std::nullopt;

    static constexpr float padding = 6.f;
    return ScreenBounds{ min_x - padding, min_y - padding, max_x + padding, max_y + padding };
}

static void draw_bounding_box(const ScreenBounds& bounds, float thickness)
{
    Overlay::draw_list->AddRect(
        ImVec2(bounds.min_x, bounds.min_y),
        ImVec2(bounds.max_x, bounds.max_y),
        g_config.esp.boxColorEnemy,
        0.f,
        ImDrawFlags_None,
        thickness);
}

static void draw_name_label(const ScreenBounds& bounds, const std::string& name, ImU32 color)
{
    const ImVec2 text_size = ImGui::CalcTextSize(name.c_str());
    const float  x         = bounds.min_x + ((bounds.max_x - bounds.min_x) - text_size.x) * 0.5f;
    const float  y         = bounds.min_y - text_size.y - 3.f;

    Overlay::draw_list->AddText(ImVec2(x + 1.f, y + 1.f), IM_COL32(0, 0, 0, 200), name.c_str());
    Overlay::draw_list->AddText(ImVec2(x,        y),       color,                  name.c_str());
}

static void draw_health_bar(const ScreenBounds& bounds, int32_t health, int32_t max_health)
{
    const float frac = std::clamp(static_cast<float>(health) / static_cast<float>(max_health), 0.f, 1.f);

    constexpr float bar_width = 4.f;
    constexpr float bar_gap   = 8.f;

    const float bar_x = bounds.min_x - bar_gap;
    const float bar_y = bounds.min_y;
    const float bar_h = bounds.max_y - bounds.min_y;

    Overlay::draw_list->AddRectFilled(
        ImVec2(bar_x,             bar_y),
        ImVec2(bar_x + bar_width, bar_y + bar_h),
        IM_COL32(0, 0, 0, 200));

    const ImU32 health_color = IM_COL32(
        static_cast<int>((1.f - frac) * 255.f),
        static_cast<int>(frac         * 255.f),
        0,
        255);

    const float filled_h  = bar_h * frac;
    const float filled_y0 = bar_y + (bar_h - filled_h);

    Overlay::draw_list->AddRectFilled(
        ImVec2(bar_x,             filled_y0),
        ImVec2(bar_x + bar_width, bar_y + bar_h),
        health_color);

    char hp_text[8];
    snprintf(hp_text, sizeof(hp_text), "%d", health);

    const ImVec2 hp_size = ImGui::CalcTextSize(hp_text);
    const float  hp_x    = bar_x - hp_size.x - 4.f;
    const float  hp_y    = filled_y0 - (hp_size.y * 0.5f);

    Overlay::draw_list->AddText(ImVec2(hp_x + 1.f, hp_y + 1.f), IM_COL32(0,   0,   0,   200), hp_text);
    Overlay::draw_list->AddText(ImVec2(hp_x,        hp_y),       IM_COL32(255, 255, 255, 255), hp_text);
}

static void draw_weapon_label(const ScreenBounds& bounds, const std::string& weapon_name)
{
    const ImVec2 size = ImGui::CalcTextSize(weapon_name.c_str());
    const float  x    = bounds.min_x + ((bounds.max_x - bounds.min_x) - size.x) * 0.5f;
    const float  y    = bounds.max_y;

    Overlay::draw_list->AddText(ImVec2(x + 1.f, y + 1.f), IM_COL32(0,   0,   0,   200), weapon_name.c_str());
    Overlay::draw_list->AddText(ImVec2(x,        y),       IM_COL32(255, 255, 255, 255), weapon_name.c_str());
}

static void draw_weapon_icon(const ScreenBounds& bounds, const std::string& weapon_name)
{
    const ImVec2 size = ImGui::CalcTextSize(weapon_name.c_str());
    const float  x    = bounds.min_x + ((bounds.max_x - bounds.min_x) - size.x) * 0.5f;
    const float  y    = bounds.max_y;
    const std::string& icon_str = get_weapon_icon(weapon_name);

    Overlay::draw_list->AddText(
        Overlay::weapon_font,
        20.f,
        ImVec2(x, y),
        IM_COL32(255, 255, 255, 255),
        icon_str.c_str());
}

static void draw_status_flags(const ScreenBounds& bounds, const PlayerInfo& player_info)
{
    constexpr float gap = 2.f;

    const float flag_x = bounds.max_x + gap;
    float       flag_y = bounds.min_y;

    const auto draw_flag = [&](const char* label, ImU32 color)
    {
        constexpr ImU32 shadow = IM_COL32(0, 0, 0, 255);

        ImGui::PushFont(Overlay::small_font);
        const ImVec2 size = ImGui::CalcTextSize(label);

        Overlay::draw_list->AddText(ImVec2(flag_x - 1.f, flag_y),        shadow, label);
        Overlay::draw_list->AddText(ImVec2(flag_x + 1.f, flag_y),        shadow, label);
        Overlay::draw_list->AddText(ImVec2(flag_x,        flag_y - 1.f), shadow, label);
        Overlay::draw_list->AddText(ImVec2(flag_x,        flag_y + 1.f), shadow, label);
        Overlay::draw_list->AddText(ImVec2(flag_x,        flag_y),       color,  label);

        ImGui::PopFont();
        flag_y += size.y + gap;
    };

    const unsigned int show = g_config.esp.player.uShowFlags;

    if (player_info.iArmor > 0 && (show & FLAG_HAS_HELMET))
    {
        if (player_info.HasFlag(FLAG_HAS_HELMET))
            draw_flag("HK", IM_COL32(100, 200, 255, 255));
        else
            draw_flag("K",  IM_COL32(150, 220, 255, 255));
    }

    if ((show & FLAG_SCOPED)      && player_info.HasFlag(FLAG_SCOPED))
        draw_flag("*SCOPED*",    IM_COL32(255, 220,  80, 255));

    if ((show & FLAG_HAS_DEFUSER) && player_info.HasFlag(FLAG_HAS_DEFUSER))
        draw_flag("KIT",         IM_COL32( 80, 220, 255, 255));

    if ((show & FLAG_DEFUSING)    && player_info.HasFlag(FLAG_DEFUSING))
        draw_flag("*DEFUSING*",  IM_COL32(255,  80,  80, 255));

    if ((show & FLAG_FLASHED)     && player_info.HasFlag(FLAG_FLASHED))
        draw_flag("*FLASHED*",   IM_COL32(255, 255,   0, 255));
}

void CESP::Run()
{
    if (!g_config.esp.player.bEnable)
        return;

    //printf("count %lu\n", g_EntityCache.m_weapons.size());

    for (const auto& weapon_ptr : g_EntityCache.get_weapons())
    {
        auto idk =  R().ReadMem<const char*>(weapon_ptr +0x28);
        printf("wwfo %s\n", idk);
    }

    auto local_team = R().ReadMem<int32_t>(g_EntityCache.m_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));

    for (const auto& [controller, pawn] : g_EntityCache.get_player_pairs())
    {
        if (!is_valid_ptr(pawn))
            continue;

        if (R().ReadMem<uint8_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_lifeState)) != 0)
            continue;

        const auto pawn_team = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iTeamNum));

        if (!g_is_ffa && !g_config.esp.player.bTeam && pawn_team == local_team)
            continue;

        const auto game_scene_node = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
        if (!is_valid_ptr(game_scene_node))
            continue;

        if (R().ReadMem<bool>(game_scene_node + SCHEMA_OFFSET(CGameSceneNode, m_bDormant)))
            continue;

        const auto bone_map = all_bones(game_scene_node);
        if (bone_map.empty())
            continue;

        const auto it_head = bone_map.find(Bones::Head);
        if (it_head == bone_map.end())
            continue;

        if (g_config.esp.player.bVisible)
        {
            const auto local_scene_node = R().ReadMem<uintptr_t>(g_EntityCache.m_local_pawn + SCHEMA_OFFSET(C_BaseEntity, m_pGameSceneNode));
            const auto local_head       = bone_position(local_scene_node, static_cast<uint64_t>(Bones::Head));
            const auto& head            = it_head->second;

            if (!g_MapManager.is_visible({ local_head.x, local_head.y, local_head.z }, { head.x, head.y, head.z }))
                continue;
        }

        if (g_config.esp.player.bSkeleton)
            DrawSkeleton(bone_map);

        // PLayer INfo Shyt

        PlayerInfo player_info{};
        player_info.szName      = R().ReadString(controller + SCHEMA_OFFSET(CBasePlayerController, m_iszPlayerName));
        player_info.iHealth     = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iHealth));
        player_info.iMaxHealth  = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_BaseEntity, m_iMaxHealth));
        player_info.iArmor      = R().ReadMem<int32_t>(pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_ArmorValue));


        const auto weapon_services = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pWeaponServices));
        const auto weapon_handle = R().ReadMem<uintptr_t>(weapon_services + SCHEMA_OFFSET(CPlayer_WeaponServices, m_hActiveWeapon));
        const auto index = reinterpret_cast<uint64_t>(weapon_handle) & 0xFFF;

        if (const auto weapon_ent = g_EntityCache.read_entity_at_index(index); is_valid_ptr(weapon_ent))
        {
            player_info.iReserveAmmo = R().ReadMem<uintptr_t>(weapon_ent + SCHEMA_OFFSET(C_BasePlayerWeapon, m_pReserveAmmo));
            player_info.iClipPrimary = R().ReadMem<uintptr_t>(weapon_ent + SCHEMA_OFFSET(C_BasePlayerWeapon, m_iClip1));

            const auto wep_def = R().ReadMem<uint16_t>(weapon_ent + SCHEMA_OFFSET(C_EconEntity, m_AttributeManager) +
                SCHEMA_OFFSET(C_AttributeContainer, m_Item) + SCHEMA_OFFSET(C_EconItemView, m_iItemDefinitionIndex));

            const auto weapon_type = weapon_type_from_index(wep_def);
            player_info.szActiveWeaponName = weapon_display_name(weapon_type);
        }

        player_info.flags = 0;

        if (R().ReadMem<int8_t>(pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_bIsScoped)))
            player_info.SetFlag(FLAG_SCOPED);

        if (R().ReadMem<int8_t>(pawn + SCHEMA_OFFSET(C_CSPlayerPawn, m_bIsDefusing)))
            player_info.SetFlag(FLAG_DEFUSING);

        if (R().ReadMem<float>(pawn + SCHEMA_OFFSET(C_CSPlayerPawnBase, m_flFlashDuration)) > 0.1f)
            player_info.SetFlag(FLAG_FLASHED);

        const auto item_services = R().ReadMem<uintptr_t>(pawn + SCHEMA_OFFSET(C_BasePlayerPawn, m_pItemServices));
        if (is_valid_ptr(item_services))
        {
            if (R().ReadMem<int8_t>(item_services + SCHEMA_OFFSET(CCSPlayer_ItemServices, m_bHasDefuser)))
                player_info.SetFlag(FLAG_HAS_DEFUSER);

            if (R().ReadMem<int8_t>(item_services + SCHEMA_OFFSET(CCSPlayer_ItemServices, m_bHasHelmet)))
                player_info.SetFlag(FLAG_HAS_HELMET);
        }

        const auto bounds = compute_screen_bounds(bone_map);
        if (!bounds.has_value())
            continue;

        if (g_config.esp.player.bName && !player_info.szName.empty())
            draw_name_label(*bounds, player_info.szName, IM_COL32_WHITE);

        if (g_config.esp.player.bHealth)
            draw_health_bar(*bounds, player_info.iHealth, player_info.iMaxHealth);

        if (g_config.esp.player.bActiveWeapon && !player_info.szActiveWeaponName.empty())
            g_config.esp.player.bActiveWeaponIcon ? draw_weapon_icon(*bounds, player_info.szActiveWeaponName) : draw_weapon_label(*bounds, player_info.szActiveWeaponName);

        draw_status_flags(*bounds, player_info);

        if (g_config.esp.player.bDraw2DBox)
            draw_bounding_box(*bounds,  1.f);
/*
 * Undetected, even tho I dont like having any write operations
        // Cache the inline struct base once — no dereference
        const uintptr_t pGlowBase = pawn + SCHEMA_OFFSET(C_BaseModelEntity, m_Glow);

        if (R().WriteMemPhysical<uint32_t>(pGlowBase + SCHEMA_OFFSET(CGlowProperty, m_glowColorOverride), 0xFF0000FF))
            if (R().WriteMemPhysical<uint8_t>(pGlowBase + SCHEMA_OFFSET(CGlowProperty, m_bGlowing), 1))
                ;
*/
    }
}

void CESP::DrawSkeleton(const std::unordered_map<Bones, Utils::Math::Vector>& bone_map, float thickness)
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

        Overlay::draw_list->AddLine(screen_a.value(), screen_b.value(), g_config.esp.skeletonColor, thickness);
    }
}