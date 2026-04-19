#ifndef CS2_LINUXEXTERNAL_CESP_H
#define CS2_LINUXEXTERNAL_CESP_H

#include <cstdint>
#include <unordered_map>
#include "Utils/Utils.h"

struct PlayerInfo;

enum class Bones : std::uint64_t
{
    Hip          = 0,
    Spine1       = 1,
    Spine2       = 2,
    Spine3       = 3,
    Spine4       = 4,
    Neck         = 5,
    Head         = 6,
    LeftShoulder  = 8,
    LeftElbow     = 9,
    LeftHand      = 10,
    RightShoulder = 13,
    RightElbow    = 14,
    RightHand     = 15,
    LeftHip       = 22,
    LeftKnee      = 23,
    LeftFoot      = 24,
    RightHip      = 25,
    RightKnee     = 26,
    RightFoot     = 27,
};

struct ScreenBounds
{
    float min_x, min_y;
    float max_x, max_y;
};

Utils::Math::Vector bone_position(uintptr_t game_scene_node, uint64_t bone_index);

class CESP
{
public:
    void Run();
    void DrawSkeleton(const std::unordered_map<Bones, Utils::Math::Vector>& bone_map, float thickness = 1.5f);
};

namespace F { inline CESP ESP; }

#endif