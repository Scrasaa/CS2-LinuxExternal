#pragma once
#include "imgui.h"

// ─────────────────────────────────────────────────────────────────────────────
// Overlay
// Full-screen transparent X11/GLX/ImGui overlay with XInput2 global hotkey.
//
// Typical usage in main():
//
//   Overlay::Start();   // blocks until g_running == false (SIGINT / window close)
//
// If you need to supply explicit geometry instead of auto-detecting the
// primary monitor, call:
//
//   Overlay::Start(x, y, w, h);
// ─────────────────────────────────────────────────────────────────────────────

namespace Overlay
{
    // Detects primary monitor geometry, then calls Init → Run → Shutdown.
    // Blocks until the render loop exits. Returns false if Init failed.
    bool Start();

    inline ImFont* small_font{nullptr};

    // Explicit geometry variant — use when you already know the target
    // window / monitor rect.
    bool Start(int x, int y, int w, int h);
}