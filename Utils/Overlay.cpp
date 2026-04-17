//
// Created by scrasa on 23.02.26.
//

#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>

#include "Overlay.h"

// X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>

// OpenGL / GLX
#include <GL/gl.h>
#include <GL/glx.h>

// ImGui
#include "Config.h"
#include "globals.h"
#include "../Thirdparty/ImGUI/imgui.h"
#include "../Thirdparty/ImGUI/backends/imgui_impl_opengl3.h"
#include "../Thirdparty/ImGUI/backends/imgui_impl_x11.h"
#include "BVH/map_manager.h"
#include "Features/CAimbot.h"
#include "Features/CESP.h"
#include "Features/CTriggerbot.h"
#include "SDK/Helper/CInput.h"
#include "SDK/Helper/ConVar.h"

namespace Overlay
{

// ─────────────────────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<bool> g_running          { true  };
static std::atomic<bool> g_menu_visible     { false };
static std::atomic<bool> g_toggle_requested { false };

static void sig_handler(int) { g_running = false; }

// ─────────────────────────────────────────────────────────────────────────────
// OverlayWindow
// ─────────────────────────────────────────────────────────────────────────────

struct OverlayWindow
{
    Display   *display  = nullptr;
    Window     window   = 0;
    GLXContext glx_ctx  = nullptr;
    int        screen   = 0;
    int        width    = 0;
    int        height   = 0;
};

static OverlayWindow g_ow;

// ─────────────────────────────────────────────────────────────────────────────
// XInput2 Raw Key Thread
//
// Separate Display connection — never share a Display* between threads.
// Raw events arrive regardless of keyboard focus, which makes this the only
// reliable global-hotkey approach for a fullscreen game on X11.
// ─────────────────────────────────────────────────────────────────────────────

struct RawInputThread
{
    std::thread thread;
    Display    *display    = nullptr;
    int         xi_opcode  = 0;

    bool init()
    {
        display = XOpenDisplay(nullptr);
        if (!display)
        {
            fprintf(stderr, "[hotkey] Cannot open secondary X display\n");
            return false;
        }

        int event, error;
        if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event, &error))
        {
            fprintf(stderr, "[hotkey] XInput2 not available\n");
            XCloseDisplay(display);
            return false;
        }

        int major = 2, minor = 0;
        if (XIQueryVersion(display, &major, &minor) == BadRequest)
        {
            fprintf(stderr, "[hotkey] XInput2 version mismatch\n");
            XCloseDisplay(display);
            return false;
        }

        XIEventMask mask{};
        mask.deviceid = XIAllDevices;
        mask.mask_len = XIMaskLen(XI_LASTEVENT);
        mask.mask     = static_cast<unsigned char*>(calloc(mask.mask_len, 1));
        XISetMask(mask.mask, XI_RawKeyPress);
        XISelectEvents(display, DefaultRootWindow(display), &mask, 1);
        XSync(display, False);
        free(mask.mask);

        return true;
    }

    void run() const
    {
        const int insert_keycode = XKeysymToKeycode(display, XK_Insert);

        while (g_running)
        {
            while (XPending(display))
            {
                XEvent xev;
                XNextEvent(display, &xev);

                if (xev.type != GenericEvent)
                    continue;

                XGenericEventCookie *cookie = &xev.xcookie;
                if (cookie->extension != xi_opcode)
                    continue;

                if (!XGetEventData(display, cookie))
                    continue;

                if (cookie->evtype == XI_RawKeyPress)
                {
                    const auto *rev = static_cast<XIRawEvent *>(cookie->data);
                    if (rev->detail == insert_keycode)
                        g_toggle_requested = true;
                }

                XFreeEventData(display, cookie);
            }

            struct timespec ts { 0, 1'000'000 };
            nanosleep(&ts, nullptr);
        }
    }

    void start()
    {
        thread = std::thread([this] { run(); });
    }

    void stop()
    {
        if (thread.joinable())
            thread.join();
        if (display)
        {
            XCloseDisplay(display);
            display = nullptr;
        }
    }
};

static RawInputThread g_hotkey_thread;

// ─────────────────────────────────────────────────────────────────────────────
// Window helpers
// ─────────────────────────────────────────────────────────────────────────────

static XVisualInfo *find_argb_visual(Display *dpy, int screen)
{
    int attribs[] =
    {
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_DOUBLEBUFFER,  True,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8,
        GLX_BLUE_SIZE,     8,
        GLX_ALPHA_SIZE,    8,
        GLX_DEPTH_SIZE,    24,
        None
    };

    int count = 0;
    GLXFBConfig *configs = glXChooseFBConfig(dpy, screen, attribs, &count);
    if (!configs || count == 0)
        return nullptr;

    for (int i = 0; i < count; i++)
    {
        XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, configs[i]);
        if (vi && vi->depth == 32)
        {
            XFree(configs);
            return vi;
        }
        XFree(vi);
    }

    XFree(configs);
    return nullptr;
}

static void set_click_through(Display *dpy, Window win)
{
    XserverRegion region = XFixesCreateRegion(dpy, nullptr, 0);
    XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(dpy, region);
}

static void set_click_receive(Display *dpy, Window win, int width, int height)
{
    XRectangle rect { 0, 0,
                      static_cast<unsigned short>(width),
                      static_cast<unsigned short>(height) };
    XserverRegion region = XFixesCreateRegion(dpy, &rect, 1);
    XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(dpy, region);
}

static void set_always_on_top(Display *dpy, Window win)
{
    Atom net_wm_state       = XInternAtom(dpy, "_NET_WM_STATE",       False);
    Atom net_wm_state_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(dpy, win, net_wm_state, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&net_wm_state_above), 1);
}

static void set_window_type_overlay(Display *dpy, Window win)
{
    Atom type      = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE",         False);
    Atom type_util = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(dpy, win, type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&type_util), 1);
}

static void set_bypass_compositor(Display *dpy, Window win)
{
    Atom bypass = XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False);
    long value  = 1; // was 2
    XChangeProperty(dpy, win, bypass, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&value), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Primary monitor geometry detection
// Uses Xinerama / XRandR output 0 if available, otherwise falls back to the
// root window dimensions.
// ─────────────────────────────────────────────────────────────────────────────

static void detect_primary_monitor(int &out_x, int &out_y, int &out_w, int &out_h)
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy)
    {
        out_x = out_y = 0;
        out_w = 1920;
        out_h = 1080;
        return;
    }

    const int screen = DefaultScreen(dpy);
    out_x = 0;
    out_y = 0;
    out_w = DisplayWidth(dpy, screen);
    out_h = DisplayHeight(dpy, screen);

    XCloseDisplay(dpy);
}

// ─────────────────────────────────────────────────────────────────────────────
// create_overlay / destroy_overlay
// ─────────────────────────────────────────────────────────────────────────────

static bool create_overlay(OverlayWindow &ow, int x, int y, int w, int h)
{
    ow.display = XOpenDisplay(nullptr);
    if (!ow.display)
    {
        fprintf(stderr, "Cannot open X display\n");
        return false;
    }

    ow.screen = DefaultScreen(ow.display);
    ow.width  = w;
    ow.height = h;

    XVisualInfo *vi = find_argb_visual(ow.display, ow.screen);
    if (!vi)
    {
        fprintf(stderr, "No ARGB GLX visual found – is compositing enabled?\n");
        XCloseDisplay(ow.display);
        return false;
    }

    const Colormap cmap = XCreateColormap(ow.display,
                              RootWindow(ow.display, ow.screen),
                              vi->visual, AllocNone);

    XSetWindowAttributes swa{};
    swa.colormap          = cmap;
    swa.border_pixel      = 0;
    swa.background_pixel  = 0;
    swa.override_redirect = True; // was false
    swa.event_mask        = ExposureMask        | StructureNotifyMask
                          | KeyPressMask        | KeyReleaseMask
                          | ButtonPressMask     | ButtonReleaseMask
                          | PointerMotionMask;

    ow.window = XCreateWindow(
        ow.display,
        RootWindow(ow.display, ow.screen),
        x, y, w, h, 0,
        vi->depth, InputOutput, vi->visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWEventMask,
        &swa);

    set_always_on_top(ow.display, ow.window);
    set_window_type_overlay(ow.display, ow.window);
    set_bypass_compositor(ow.display, ow.window);

    // Strip title bar via Motif hints
    struct { long flags, functions, decorations, input_mode, status; } mwm{};
    mwm.flags       = 2;
    mwm.decorations = 0;
    Atom mwm_atom = XInternAtom(ow.display, "_MOTIF_WM_HINTS", False);
    XChangeProperty(ow.display, ow.window, mwm_atom, mwm_atom, 32,
                    PropModeReplace, reinterpret_cast<unsigned char *>(&mwm), 5);

    Atom wm_delete = XInternAtom(ow.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ow.display, ow.window, &wm_delete, 1);

    XStoreName(ow.display, ow.window, "overlay");

    ow.glx_ctx = glXCreateContext(ow.display, vi, nullptr, GL_TRUE);
    XFree(vi);

    if (!ow.glx_ctx)
    {
        fprintf(stderr, "Failed to create GLX context\n");
        XDestroyWindow(ow.display, ow.window);
        XCloseDisplay(ow.display);
        return false;
    }

    glXMakeCurrent(ow.display, ow.window, ow.glx_ctx);
    set_click_through(ow.display, ow.window);

    XMapWindow(ow.display, ow.window);
    XFlush(ow.display);

    return true;
}

static void destroy_overlay(const OverlayWindow &ow)
{
    if (ow.glx_ctx)
    {
        glXMakeCurrent(ow.display, None, nullptr);
        glXDestroyContext(ow.display, ow.glx_ctx);
    }
    if (ow.window)  XDestroyWindow(ow.display, ow.window);
    if (ow.display) XCloseDisplay(ow.display);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

static bool init(int x, int y, int w, int h)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    if (!g_hotkey_thread.init())
        return false;

    if (!create_overlay(g_ow, x, y, w, h))
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;

    // Must be set before the first NewFrame() — ImGui_ImplX11_NewFrame() does
    // not query the window size itself, so we own this value.
    io.DisplaySize = ImVec2(static_cast<float>(g_ow.width),
                            static_cast<float>(g_ow.height));

    const auto def_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    14.0f
    );

    io.FontDefault = def_font;

    small_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    12.0f
    );

    ImGui::StyleColorsDark();

    ImGui_ImplX11_Init(g_ow.display, g_ow.window);
    ImGui_ImplOpenGL3_Init("#version 130");

    g_hotkey_thread.start();

    return true;
}
// ─────────────────────────────────────────────────────────────────────────────
// Menu Design
// ─────────────────────────────────────────────────────────────────────────────

int idxEsp = 0;
char cfgNameBuf[32] = "";


int selectedCfgIndex = -1;

void DrawFileSelector()
{
    ImGui::Text("Select a config file:");

    const auto configs = cfg::GetConfigFiles();
    float listHeight = ImGui::GetTextLineHeightWithSpacing() * configs.size();

    ImGui::BeginChild("FileList", ImVec2(0, listHeight), true); // scrollable area
    {
        for (int i = 0; i < configs.size(); i++)
        {
            const bool isSelected = (i == selectedCfgIndex);

            if (ImGui::Selectable(configs[i].c_str(), isSelected))
                selectedCfgIndex = i;
        }
        ImGui::EndChild();
    }

    if (selectedCfgIndex >= 0 && ImGui::Button("Save selected"))
        cfg::Save(configs[selectedCfgIndex]);


    if (selectedCfgIndex >= 0 && ImGui::Button("Load selected"))
        cfg::Load(configs[selectedCfgIndex]);


    if (selectedCfgIndex >= 0 && ImGui::Button("Delete selected"))
        cfg::Delete(configs[selectedCfgIndex]);
}

bool CheckboxCompact(const char* label, bool* v)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 1.f));
    bool changed = ImGui::Checkbox(label, v);
    ImGui::PopStyleVar();
    return changed;
}

bool SliderFloatCompact(const char* label, float* v, float min, float max, const char* format = "%1.f")
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 1.f));
    bool changed = ImGui::SliderFloat(label, v, min, max, format);
    ImGui::PopStyleVar();
    return changed;
}

bool SliderIntCompact(const char* label, int* v, int min, int max, const char* format = "%d")
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 1.f));
    bool changed = ImGui::SliderInt(label, v, min, max, format);
    ImGui::PopStyleVar();
    return changed;
}

void DrawMenu()
{
    ImGui::Begin("Linux External - [DEV BUILD]", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImGui::SetWindowSize(ImVec2(600, 400));

    if (ImGui::BeginTabBar("MainTabs"))
    {
        // Aimbot tab
        if (ImGui::BeginTabItem("Aimbot"))
        {
            ImGui::BeginChild("AimbotLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
            ImGui::Text("Aimbot Settings");
            ImGui::Separator();

            CheckboxCompact("Enable Aimbot", &g_config.aimbot.bEnable);
            CheckboxCompact("Visibility Check", &g_config.aimbot.bVisible);
            CheckboxCompact("Enable Auto Shoot", &g_config.aimbot.bAutoShoot);
            ImGui::Separator();
            SliderFloatCompact("Aim Radius", &g_config.aimbot.fRadius, 1.f, 2560.f, "%1.f");
            SliderFloatCompact("Aimbot Smoothness", &g_config.aimbot.fSmoothness, 0.01f, 1.f, "%.2f");

            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("AimbotRight", ImVec2(0, 0), true);
            ImGui::Text("Weapon Settings");
            ImGui::Separator();
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Triggerbot"))
        {
            ImGui::BeginChild("TriggerbotLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
            ImGui::Text("Triggerbot Settings");
            ImGui::Separator();

            CheckboxCompact("Enable Triggerbot", &g_config.triggerbot.bEnable);
            ImGui::Separator();
            SliderIntCompact("Min Reaction (ms)", &g_config.triggerbot.iMinReaction, 1, 300);
            SliderIntCompact("Max Reaction", &g_config.triggerbot.iMaxReaction, 1, 300);

            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("ESP"))
        {
            ImGui::BeginChild("ESPLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
            {
                ImGui::Text("ESP Settings");
                ImGui::Separator();

                switch (idxEsp)
                {
                    case 0:
                    {
                        // Players
                        CheckboxCompact("Enable ESP", &g_config.esp.player.bEnable);
                        CheckboxCompact("Draw 2D Box", &g_config.esp.player.bDraw2DBox);
                        CheckboxCompact("Draw Name", &g_config.esp.player.bName);
                        CheckboxCompact("Draw Health", &g_config.esp.player.bHealth);
                        CheckboxCompact("Only Visible", &g_config.esp.player.bVisible);
                        CheckboxCompact("Draw Weapon", &g_config.esp.player.bWeapon);
                        CheckboxCompact("Draw Skeleton", &g_config.esp.player.bSkeleton);

                        ImGui::SeparatorText("Flag Indicators");

                        auto& show = g_config.esp.player.uShowFlags;
                        for (const auto& meta : k_flag_meta)
                            ImGui::CheckboxFlags(meta.label.data(), &show, static_cast<unsigned int>(meta.flag));
                        break;
                    }
                    case 1:
                    {
                        // Weapons
                        break;
                    }
                    case 2:
                    {
                        // C4
                        break;
                    }
                    case 3:
                    {
                        // Chicken?
                        break;
                    }
                    default:;
                }
                ImGui::EndChild();
            }

            ImGui::SameLine();

            ImGui::BeginChild("ESPRight", ImVec2(0, 0), true);
            {
                ImGui::Text("Filter Settings");
                ImGui::Separator();
                // dropdown select? test

                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Visuals"))
        {
            ImGui::BeginChild("VisualsLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
            ImGui::Text("Visual Settings");
            ImGui::Separator();

            CheckboxCompact("Draw Aimbot FOV", &g_config.visuals.bDrawFovCircle);
            CheckboxCompact("Draw Snaplines", &g_config.visuals.bDrawSnapLines);

            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("VisualsRight", ImVec2(0, 0), true);
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Color"))
        {
            ImGui::ColorEdit4("Skeleton Color", reinterpret_cast<float *>(&g_config.esp.skeletonColor.Value));
            ImGui::ColorEdit4("Box Color", reinterpret_cast<float *>(&g_config.esp.boxColorEnemy.Value));

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Config"))
        {
            DrawFileSelector();

            ImGui::InputText("##input", cfgNameBuf, IM_ARRAYSIZE(cfgNameBuf));

            if (ImGui::Button("Create new config"))
            {
                cfg::Save(cfgNameBuf);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
// ─────────────────────────────────────────────────────────────────────────────
// Run
// ─────────────────────────────────────────────────────────────────────────────
std::once_flag initFlag;
static void run()
{
    const Atom wm_delete = XInternAtom(g_ow.display, "WM_DELETE_WINDOW", False);

    while (g_running)
    {
        // ── Handle pending toggle request from hotkey thread ──────────────────
        if (g_toggle_requested.exchange(false))
        {
            g_menu_visible = !g_menu_visible;

            if (g_menu_visible)
            {
                set_click_receive(g_ow.display, g_ow.window, g_ow.width, g_ow.height);
                XSetInputFocus(g_ow.display, g_ow.window, RevertToParent, CurrentTime);
            }
            else
            {
                set_click_through(g_ow.display, g_ow.window);
            }
        }

        // ── Drain X11 events ──────────────────────────────────────────────────
        while (XPending(g_ow.display))
        {
            XEvent xev;
            XNextEvent(g_ow.display, &xev);

            ImGui_ImplX11_ProcessEvent(&xev);

            if (xev.type == ClientMessage
                && static_cast<Atom>(xev.xclient.data.l[0]) == wm_delete)
            {
                g_running = false;
            }
        }

        static uint64_t u64ButtonBase = {};

        std::call_once(initFlag, []()
        {
            auto input_mod               = R().GetModuleBase( "libinputsystem.so");
            auto u64InputBase            = R().GetInterfaceOffset(input_mod,"InputSystemVersion0").value();
            auto u64VtFn                        = R().ReadMem<uint64_t>( R().ReadMem<uint64_t>(u64InputBase) + 19 * 8); // vtable slot 19
            auto u32BtnOff                      =  R().ReadMem<uint32_t>(u64VtFn + 0x14);

            u64ButtonBase = u64InputBase + u32BtnOff;
        });

        // ── ImGui frame ───────────────────────────────────────────────────────
        // ImGui_ImplX11_NewFrame() does not update DisplaySize itself —
        // we must set it every frame so the assertion never fires.
        ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(g_ow.width),
                                            static_cast<float>(g_ow.height));

        g_screen_w = static_cast<float>(g_ow.width);
        g_screen_h = static_cast<float>(g_ow.height);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplX11_NewFrame();
        ImGui::NewFrame();

        // Always-visible HUD label
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.50f);
        ImGui::Begin("##status", nullptr,
                     ImGuiWindowFlags_NoDecoration  |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoNav          |
                     ImGuiWindowFlags_NoMove);
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                           g_menu_visible ? "Menu OPEN" : "Overlay active");
        ImGui::Text("%.0f fps",  ImGui::GetIO().Framerate);
        ImGui::End();

        if (g_menu_visible)
        {
            // ── Draw your menu here ───────────────────────────────────────────
            DrawMenu();
        }

        Input g_input{};

        auto fn_read = [](uint64_t u64_addr, void* p_dst, size_t sz) -> bool
        {
            return R().ReadRaw(u64_addr, p_dst, sz);
        };

        // in your loop:
        g_input.update(fn_read, u64ButtonBase);

        g_MapManager.update();
        g_is_ffa = IsFFA(g_offsets);
        F::ESP.Run();
        if (g_input.is_key_pressed(CS2KeyCode::MouseLeft)) F::Aimbot.Run();
        if (g_input.is_key_pressed(CS2KeyCode::Mouse5)) F::Triggerbot.Run();

        ImGui::Render();

        glViewport(0, 0, g_ow.width, g_ow.height);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glXSwapBuffers(g_ow.display, g_ow.window);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

static void shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplX11_Shutdown();
    ImGui::DestroyContext();

    destroy_overlay(g_ow);

    g_hotkey_thread.stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — Start()
// ─────────────────────────────────────────────────────────────────────────────

bool Start()
{
    int x, y, w, h;
    detect_primary_monitor(x, y, w, h);
    return Start(x, y, w, h);
}

bool Start(int x, int y, int w, int h)
{
    if (!init(x, y, w, h))
    {
        shutdown();
        return false;
    }

    run();
    shutdown();
    return true;
}

} // namespace Overlay