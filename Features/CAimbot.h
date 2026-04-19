//
// Created by scrasa on 04.03.26.
//

#ifndef CS2_LINUXEXTERNAL_CAIMBOT_H
#define CS2_LINUXEXTERNAL_CAIMBOT_H
#include <cstdint>


class CAimbot
{
public:
    uintptr_t GetClosestToScreen();
    void Run();
};

namespace F { inline CAimbot Aimbot; }

#endif //CS2_LINUXEXTERNAL_CAIMBOT_H