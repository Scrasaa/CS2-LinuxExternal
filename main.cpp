#include <algorithm>
#include <csignal>
#include <iostream>
#include <vector>
#include <fcntl.h>
#include <iomanip>

#include "Features/CAimbot.h"
#include "SDK/Helper/CEntityCache.h"
#include "Utils/Utils.h"
#include "Utils/Overlay.h"

#include "SDK/Helper/CSchemaManager.h"
#include "Utils/BVH/map_manager.h"

CEntityCache g_EntityCache{0};
MapManager g_map_manager;
uintptr_t g_global_vars = 0;

int main()
{
    const std::vector<pid_t> pids = CUtils::FindPidsByName("cs2");
    if (pids.empty())
    {
        std::cerr << "[-] CS2 process not found.\n";
        return 1;
    }

    const pid_t target_pid = pids.front();
    printf("[+] Target PID: %d\n", target_pid);

    const int fd = open("/dev/kdriver", O_RDWR);
    if (fd < 0)
    {
        perror("[-] open /dev/kdriver");
        return 1;
    }

    CUtils::Init(fd, target_pid);
    auto& utils = CUtils::Get();

    const uintptr_t pattern_hit_schema = utils.PatternScan(
        "libschemasystem.so",
        "48 8B 07 48 85 D2 48 0F 44 D1 31 C9 FF 90 ? ? ? ? 48 8B 43");

    if (!pattern_hit_schema)
    {
        std::cerr << "[-] Pattern not found in libschemasystem.so.\n";
        CUtils::Shutdown();
        return 1;
    }

    const uintptr_t schema_system = utils.GetAbsoluteAddress(pattern_hit_schema + 0x35, 3, 7);
    if (!schema_system)
    {
        std::cerr << "[-] Failed to resolve CSchemaSystem.\n";
        CUtils::Shutdown();
        return 1;
    }

    printf("[+] CSchemaSystem @ 0x%llX\n", static_cast<unsigned long long>(schema_system));

    CSchemaManager::Get().Init(schema_system);
    CSchemaManager::Get().DumpAllScopes("/tmp/cs2_scope.txt");
    CSchemaManager::Get().DumpAllClasses("/tmp/cs2_classes.txt");
    CSchemaManager::Get().DumpAllClassFields("/tmp/cs2_offsets.txt");

    // Is the VMatrix situation similar to Source 1?, ours was wrong, REVERSE
    const auto pattern_hit_vmatrix = utils.PatternScan("libclient.so",
    "C6 83 ? ? 00 00 01 4C 8D 05");
   // const auto pattern_hit_vmatrix = utils.PatternScan("libclient.so",
      //  "48 8D 0D ? ? ? ? F7 85 ? ? ? ? ? ? ? ? 74 ? 48 8B 8D ? ? ? ? 69 01 ? ? ? ? 31 F6 48 89 DF 69 49");
      //
    if (!pattern_hit_vmatrix)
    {
        std::cerr << "[-] Pattern for vmatrix not found in libclient.so.\n";
        CUtils::Shutdown();
        return 1;
    }

    //const auto vmatrix = utils.GetAbsoluteAddress(pattern_hit_vmatrix, 3, 7)
    Utils::Math::vmatrix_addr = utils.GetAbsoluteAddress(pattern_hit_vmatrix + 0x0A, 0x0, 0x04);
    if (!Utils::Math::vmatrix_addr )
    {
        std::cerr << "[-] Failed to resolve vmatrix.\n";
        CUtils::Shutdown();
        return 1;
    }

    printf("ViewMatrix 0x%llx\n", static_cast<unsigned long long>(Utils::Math::vmatrix_addr ));

    // recheck what this is "libclient.so","48 8D 3D ? ? ? ? E8 ? ? ? ? 48 89 43", engine does not contain CEntitySystem its in client!
    const auto pattern_hit_entity_system = utils.PatternScan("libengine2.so", "48 8B 3D ? ? ? ? 48 8B 07 FF 90 ? ? ? ? 4C 89 E2 31 FF");
    if (!pattern_hit_entity_system)
    {
        std::cerr << "[-] Pattern for entitySystem not found in libclient.so.\n";
        CUtils::Shutdown();
        return 1;
    }

    const auto entity_system = utils.GetAbsoluteAddress(pattern_hit_entity_system, 3, 7);
    if (!entity_system)
    {
        std::cerr << "[-] Failed to resolve entity_system.\n";
        CUtils::Shutdown();
        return 1;
    }

    printf("EntitySystem 0x%llx\n", static_cast<unsigned long long>(entity_system));

    const auto pattern_hit_global_vars = R().PatternScan("libclient.so", "48 8D 05 ? ? ? ? 48 8B 00 8B 50 ? E9");
    if (!pattern_hit_global_vars)
    {
        std::cerr << "[-] Pattern for pattern_hit_global_vars not found in libclient.so.\n";
        CUtils::Shutdown();
        return 1;
    }

    g_global_vars = R().GetAbsoluteAddress(pattern_hit_global_vars, 0x3, 0x7);

    if (!g_global_vars)
    {
        std::cerr << "[-] Failed to resolve g_global_vars.\n";
        CUtils::Shutdown();
        return 1;
    }

    g_map_manager = MapManager
    (
                g_global_vars,
    [](uintptr_t addr)                      { return R().ReadMem<uintptr_t>(addr); },
    [](uintptr_t addr, std::size_t n)
    {return R().ReadString(addr, n);},
    "/home/scrasa/CLionProjects/CS2-LinuxExternal/Thirdparty/cli-linux-x64/Source2Viewer-CLI"
    );

    auto engine_mod = utils.GetModuleBase( "libengine2.so");

    auto interface_offset = utils.GetInterfaceOffset(engine_mod, "GameResourceServiceClientV001");

    if (!interface_offset.has_value())
    {
        // handle error    {
        std::cerr << "[-] Failed to resolve entity_list.\n";
        CUtils::Shutdown();
        return 1;
    }

    uintptr_t entity_interface = utils.ReadMem<uintptr_t>(interface_offset.value() + 0x50) + 0x10;

    if (!entity_interface)
    {
        // handle error    {
        std::cerr << "[-] Failed to resolve entity_interface.\n";
        CUtils::Shutdown();
        return 1;
    }

    g_EntityCache = CEntityCache{entity_interface};

    const auto pattern_hit_lp = utils.PatternScan("libclient.so", "48 83 3D ? ? ? ? ? 0F 95 C0 C3");

    if (!pattern_hit_lp)
    {
        std::cerr << "[-] Pattern for localplayer not found in libclient.so.\n";
        CUtils::Shutdown();
        return 1;
    }

    g_EntityCache.m_p_localplayer = utils.GetAbsoluteAddress(pattern_hit_lp, 3, 8);
    if (!g_EntityCache.m_p_localplayer)
    {
        std::cerr << "[-] Failed to resolve g_EntityCache.m_p_localplayer.\n";
        CUtils::Shutdown();
        return 1;
    }

    printf("LocalPlayer 0x%llx\n", static_cast<unsigned long long>(g_EntityCache.m_p_localplayer));

    auto bTest  = g_EntityCache.refresh();

    printf("Entity count %d\n", g_EntityCache.get_count());

    printf("LocalPlayer name: %s\n", R().ReadString(g_EntityCache.m_p_localplayer_controller + 0x878).c_str());

    g_screen_w = 2560;
    g_screen_h = 1440;

    Overlay::Start(0,0,2560, 1440);

    CUtils::Shutdown();
    return 0;
}