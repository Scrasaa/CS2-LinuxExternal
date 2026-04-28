#pragma once

#include "Utils/Utils.h"

// ── CRCS ──────────────────────────────────────────────────────
//
//  Delta-based recoil compensation.
//
//  Each frame Run() reads the current m_aimPunchAngle, computes the
//  change since the previous frame, scales it by the per-axis config
//  factors, and injects a compensating mouse delta so the crosshair
//  tracks back toward the pre-shot position.
//
//  Reset() must be called whenever the local player dies, respawns,
//  or holsters/switches weapons so stale punch state is discarded.

class CRCS
{
public:
    void    Run()       noexcept;
    void    Reset()     noexcept;

private:
    // Prefixed Hungarian: storage(m_) + type(b/v/f) for RE clarity
    bool                    m_b_initialized  = false;
    ImVec2  m_v_last_punch   = {};
};

namespace F {inline CRCS RCS;};