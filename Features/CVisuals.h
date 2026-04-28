//
// Created by scrasa on 19.04.26.
//

#ifndef CS2_LINUXEXTERNAL_CVISUALS_H
#define CS2_LINUXEXTERNAL_CVISUALS_H

class CVisuals
{
public:
    void DrawFOVIndicator();
    void DrawSpectatorList();
    void DrawRecoilCrosshair();
};

namespace F {inline CVisuals Visuals;}

#endif //CS2_LINUXEXTERNAL_CVISUALS_H
