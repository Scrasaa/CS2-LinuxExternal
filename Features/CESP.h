#ifndef CS2_LINUXEXTERNAL_CESP_H
#define CS2_LINUXEXTERNAL_CESP_H

#include <cstdint>
#include <unordered_map>
#include "Utils/Utils.h"

struct PlayerInfo;

enum class Bones : std::uint64_t
{
    Hip           = 1,
    Spine1        = 2,
    Spine2        = 3,
    Spine3        = 4,
    Spine4        = 23,
    Neck          = 6,
    Head          = 7,
    LeftShoulder  = 9,
    LeftElbow     = 10,
    LeftHand      = 11,
    RightShoulder = 13,
    RightElbow    = 14,
    RightHand     = 15,
    LeftHip       = 17,
    LeftKnee      = 18,
    LeftFoot      = 19,
    RightHip      = 20,
    RightKnee     = 21,
    RightFoot     = 22,
};

struct ScreenBounds
{
    float min_x, min_y;
    float max_x, max_y;
};

Utils::Math::Vector bone_position(uintptr_t game_scene_node, uint64_t bone_index);

class CESP // namespace would be better
{
public:
    void Run();
};

namespace F { inline CESP ESP; }

#endif