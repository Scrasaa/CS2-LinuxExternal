#include "imgui.h"
#include "imgui_impl_x11.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <X11/Xutil.h>

// ─── State ──────────────────────────────────────────────────────────────────

static Display *g_display = nullptr;
static Window   g_window  = 0;

using Clock     = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;
static TimePoint g_last_time{};

// ─── Key mapping ────────────────────────────────────────────────────────────

static ImGuiKey keysym_to_imgui(KeySym ks) {
    switch (ks) {
    case XK_Tab:        return ImGuiKey_Tab;
    case XK_Left:       return ImGuiKey_LeftArrow;
    case XK_Right:      return ImGuiKey_RightArrow;
    case XK_Up:         return ImGuiKey_UpArrow;
    case XK_Down:       return ImGuiKey_DownArrow;
    case XK_Page_Up:    return ImGuiKey_PageUp;
    case XK_Page_Down:  return ImGuiKey_PageDown;
    case XK_Home:       return ImGuiKey_Home;
    case XK_End:        return ImGuiKey_End;
    case XK_Insert:     return ImGuiKey_Insert;
    case XK_Delete:     return ImGuiKey_Delete;
    case XK_BackSpace:  return ImGuiKey_Backspace;
    case XK_Return:     return ImGuiKey_Enter;
    case XK_KP_Enter:   return ImGuiKey_KeypadEnter;
    case XK_Escape:     return ImGuiKey_Escape;
    case XK_space:      return ImGuiKey_Space;
    case XK_Control_L:
    case XK_Control_R:  return ImGuiKey_LeftCtrl;
    case XK_Shift_L:
    case XK_Shift_R:    return ImGuiKey_LeftShift;
    case XK_Alt_L:
    case XK_Alt_R:      return ImGuiKey_LeftAlt;
    default:
        if (ks >= XK_a && ks <= XK_z) return (ImGuiKey)(ImGuiKey_A + (ks - XK_a));
        if (ks >= XK_A && ks <= XK_Z) return (ImGuiKey)(ImGuiKey_A + (ks - XK_A));
        if (ks >= XK_0 && ks <= XK_9) return (ImGuiKey)(ImGuiKey_0 + (ks - XK_0));
        if (ks >= XK_F1 && ks <= XK_F12) return (ImGuiKey)(ImGuiKey_F1 + (ks - XK_F1));
        return ImGuiKey_None;
    }
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool ImGui_ImplX11_Init(Display *display, Window window) {
    g_display  = display;
    g_window   = window;
    g_last_time = Clock::now();

    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_x11";

    // We handle keyboard; mouse is passthrough so we skip it.
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    return true;
}

void ImGui_ImplX11_Shutdown() {
    g_display = nullptr;
    g_window  = 0;
}

void ImGui_ImplX11_NewFrame() {
    ImGuiIO &io = ImGui::GetIO();

    // Delta time
    auto now  = Clock::now();
    float dt  = std::chrono::duration<float>(now - g_last_time).count();
    io.DeltaTime  = dt > 0.0f ? dt : 1.0f / 60.0f;
    g_last_time   = now;

    // Modifier keys (poll state rather than rely on events so we don't miss them)
    if (g_display) {
        char key_state[32];
        XQueryKeymap(g_display, key_state);

        auto key_down = [&](KeySym ks) -> bool {
            KeyCode kc = XKeysymToKeycode(g_display, ks);
            return kc && (key_state[kc / 8] & (1 << (kc % 8)));
        };

        io.AddKeyEvent(ImGuiMod_Ctrl,  key_down(XK_Control_L) || key_down(XK_Control_R));
        io.AddKeyEvent(ImGuiMod_Shift, key_down(XK_Shift_L)   || key_down(XK_Shift_R));
        io.AddKeyEvent(ImGuiMod_Alt,   key_down(XK_Alt_L)     || key_down(XK_Alt_R));
    }
}

bool ImGui_ImplX11_ProcessEvent(const XEvent *event) {
    if (!event) return false;

    ImGuiIO &io = ImGui::GetIO();

    switch (event->type) {
    case KeyPress:
    case KeyRelease: {
        const XKeyEvent *ke = &event->xkey;
        KeySym ks = XLookupKeysym(const_cast<XKeyEvent *>(ke), 0);
        ImGuiKey imgui_key = keysym_to_imgui(ks);

        bool pressed = (event->type == KeyPress);

        if (imgui_key != ImGuiKey_None)
            io.AddKeyEvent(imgui_key, pressed);

        // Character input
        if (pressed) {
            char buf[8] = {};
            int len = XLookupString(const_cast<XKeyEvent *>(ke), buf, sizeof(buf) - 1, nullptr, nullptr);
            for (int i = 0; i < len; ++i)
                if ((unsigned char)buf[i] >= 32)
                    io.AddInputCharacter((unsigned char)buf[i]);
        }
        return true;
    }

    // Even though the window is click-through we forward any motion events
    // that arrive (e.g. if someone temporarily disables passthrough for a menu).
    case MotionNotify:
        io.AddMousePosEvent((float)event->xmotion.x, (float)event->xmotion.y);
        return true;

    case ButtonPress:
    case ButtonRelease: {
        bool pressed = (event->type == ButtonPress);
        int  btn     = event->xbutton.button;
        if (btn == Button1) io.AddMouseButtonEvent(0, pressed);
        if (btn == Button2) io.AddMouseButtonEvent(2, pressed);
        if (btn == Button3) io.AddMouseButtonEvent(1, pressed);
        if (btn == Button4) io.AddMouseWheelEvent(0.0f,  1.0f);
        if (btn == Button5) io.AddMouseWheelEvent(0.0f, -1.0f);
        return true;
    }

    default:
        break;
    }

    return false;
}