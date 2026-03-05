#pragma once
// ImGui backend for raw X11 (no SDL/GLFW).
// Handles keyboard input and time delta.
// Mouse is NOT forwarded because the window is click-through;
// if you ever want a non-passthrough mode just add the pointer events below.

#include <X11/Xlib.h>

#include "../imgui.h"

IMGUI_IMPL_API bool ImGui_ImplX11_Init(Display *display, Window window);
IMGUI_IMPL_API void ImGui_ImplX11_Shutdown();
IMGUI_IMPL_API void ImGui_ImplX11_NewFrame();

// Call this for every XEvent you receive in your event loop.
IMGUI_IMPL_API bool ImGui_ImplX11_ProcessEvent(const XEvent *event);