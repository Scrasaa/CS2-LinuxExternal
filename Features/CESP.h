//
// Created by scrasa on 04.03.26.
//

#ifndef CS2_LINUXEXTERNAL_CESP_H
#define CS2_LINUXEXTERNAL_CESP_H
#include <cstdint>
#include <unordered_map>

#include "imgui.h"
#include "Utils/Utils.h"

enum class Bones : std::uint64_t
{
    Hip = 0,
    Spine1 = 1,
    Spine2 = 2,
    Spine3 = 3,
    Spine4 = 4,
    Neck = 5,
    Head = 6,

    LeftShoulder = 8,
    LeftElbow = 9,
    LeftHand = 10,

    RightShoulder = 13,
    RightElbow = 14,
    RightHand = 15,

    LeftHip = 22,
    LeftKnee = 23,
    LeftFoot = 24,

    RightHip = 25,
    RightKnee = 26,
    RightFoot = 27,
};

Utils::Math::Vector bone_position(const uintptr_t game_scene_node, const uint64_t bone_index);

class CESP
{
public:
    void Run();
    static void DrawSkeleton(ImDrawList* p_draw_list, const std::unordered_map<Bones, Utils::Math::Vector>& bone_map,
                  ImU32 color = IM_COL32(255, 0, 0, 255), float thickness = 1.5f);
    static void Draw2DBox(ImDrawList* p_draw_list,
               const std::unordered_map<Bones, Utils::Math::Vector>& bone_map,
               const std::string& name,
               ImU32 box_color  = IM_COL32(255, 255, 255, 255),
               ImU32 text_color = IM_COL32(255, 255, 255, 255),
               float thickness  = 1.5f);

    // Visual Stuff
    static void DrawFOVIndicator(ImDrawList* p_draw_list, uintptr_t local_pawn);
    static void DrawSpectatorList(ImDrawList* p_draw_list, uintptr_t local_pawn);
};



namespace F { inline CESP ESP; }


#endif //CS2_LINUXEXTERNAL_CESP_H