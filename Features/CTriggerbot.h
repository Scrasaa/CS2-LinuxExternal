//
// Created by scrasa on 05.03.26.
//

#ifndef CS2_LINUXEXTERNAL_CTRIGGERBOT_H
#define CS2_LINUXEXTERNAL_CTRIGGERBOT_H
#include <chrono>


class CTriggerbot
{
public:
    std::optional<std::chrono::steady_clock::time_point> m_acquire_time;
    int m_reaction_ms = 0;
    void Run();
};

namespace F {inline CTriggerbot Triggerbot;}


#endif //CS2_LINUXEXTERNAL_CTRIGGERBOT_H