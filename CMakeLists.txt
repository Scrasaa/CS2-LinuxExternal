cmake_minimum_required(VERSION 3.20)
project(CS2_LinuxExternal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find system packages ───────────────────────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)

# ── ImGui source root ─────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/Thirdparty/ImGUI)

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(CS2_LinuxExternal
        main.cpp

        # Utils
        Utils/Utils.cpp
        Utils/Utils.h

        # Overlay
        Utils/Overlay.cpp
        Utils/Overlay.h

        # SDK
        SDK/defs.h
        SDK/Helper/CSchemaManager.cpp
        SDK/Helper/CSchemaManager.h

        # ImGui core
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp

        # ImGui backends
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_x11.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3_loader.h
        SDK/Helper/CEntityCache.cpp
        SDK/Helper/CEntityCache.h
        Features/CAimbot.cpp
        Features/CAimbot.h
        Features/CESP.cpp
        Features/CESP.h
        SDK/Helper/CInput.cpp
        SDK/Helper/CInput.h
        SDK/key_codes.h
        SDK/key_codes.cpp
        Utils/BVH/Bvh.h
        Utils/BVH/map_visibility.h
        Utils/BVH/map_manager.h
        Utils/BVH/map_manager.cpp
        Utils/BVH/map_parser.h
        Utils/BVH/dmx_parser.h
        Features/CTriggerbot.cpp
        Features/CTriggerbot.h
        globals.h
        Utils/Config.cpp
        Utils/Config.h
        Thirdparty/json.h
        SDK/Helper/ConVar.h
        SDK/Helper/ConVar.cpp
        Features/CVisuals.cpp
        Features/CVisuals.h
        SDK/weapon_type.h
        SDK/player_info.h
        Features/CRecoilControlSystem.cpp
        Features/CRecoilControlSystem.h
)

# ── Include dirs ──────────────────────────────────────────────────────────────
target_include_directories(CS2_LinuxExternal PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${X11_INCLUDE_DIR}
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(CS2_LinuxExternal PRIVATE
        # X11 + extensions needed by Overlay
        ${X11_LIBRARIES}
        ${X11_Xfixes_LIB}
        X11::X11
        Xext                    # XShape
        Xi                      # XInput2

        # OpenGL + GLX
        OpenGL::GL
        OpenGL::GLX
)