#pragma once
#include <map>

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
static std::map<std::wstring, std::wstring> weapon_icon_table =
{
	{(L"deagle"), (L"\uE001")},
	{(L"elite"), (L"\uE002")},
	{(L"fiveseven"), (L"\uE003")},
	{(L"glock"), (L"\uE004")},
	{(L"ak47"), (L"\uE007")},
	{(L"aug"), (L"\uE008")},
	{(L"awp"), (L"\uE009")},
	{(L"famas"), (L"\uE00a")},
	{(L"g3sg1"), (L"\uE00b")},
	{(L"galilar"), (L"\uE00d")},
	{(L"m249"), (L"\uE03c")},
	{(L"m4a1"), (L"\uE00e")},
	{(L"mac10"), (L"\uE011")},
	{(L"p90"), (L"\uE024")},
	{(L"mp5sd"), (L"mp5sd")},
	{(L"ump45"), (L"\uE018")},
	{(L"xm1014"), (L"\uE019")},
	{(L"bizon"), (L"\uE01a")},
	{(L"mag7"), (L"\uE01b")},
	{(L"negev"), (L"\uE01c")},
	{(L"sawedoff"), (L"\uE01d")},
	{(L"tec9"), (L"\uE01e")},
	{(L"taser"), (L"\uE01f")},
	{(L"hkp2000"), (L"\uE013")},
	{(L"mp7"), (L"\uE021")},
	{(L"mp9"), (L"\uE022")},
	{(L"nova"), (L"\uE023")},
	{(L"p250"), (L"\uE020")},
	{(L"shield"), (L"shield")},
	{(L"scar20"), (L"\uE026")},
	{(L"sg556"), (L"\uE027")},
	{(L"ssg08"), (L"\uE028")},
	{(L"knife_gg"), (L"knife_gg")},
	{(L"knife"), (L"\uE02a")},
	{(L"flashbang"), (L"\uE02b")},
	{(L"hegrenade"), (L"\uE02c")},
	{(L"smokegrenade"), (L"\uE02d")},
	{(L"molotov"), (L"\uE02e")},
	{(L"decoy"), (L"\uE02f")},
	{(L"incgrenade"), (L"\uE030")},
	{(L"c4"), (L"\uE031")},
	{(L"knife_t"), (L"\uE03b")},
	{(L"m4a1_silencer"), (L"\uE010")},
	{(L"usp_silencer"), (L"\uE03d")},
	{(L"cz75a"), (L"\uE03f")},
	{(L"revolver"), (L"\uE040")},
	{(L"knife_bayonet"), (L"\uE1f4")},
	{(L"knife_css"), (L"\uE02a")},
	{(L"knife_flip"), (L"\uE1f9")},
	{(L"knife_gut"), (L"\uE1fa")},
	{(L"knife_karambit"), (L"\uE1fb")},
	{(L"knife_m9_bayonet"), (L"\uE1fc")},
	{(L"knife_tactical"), (L"\uE1fd")},
	{(L"knife_falchion"), (L"\uE200")},
	{(L"knife_survival_bowie"), (L"\uE202")},
	{(L"knife_butterfly"), (L"\uE203")},
	{(L"knife_push"), (L"\uE204")},
	{(L"knife_cord"), (L"\uE02a")},
	{(L"knife_canis"), (L"\uE02a")},
	{(L"knife_ursus"), (L"\uE02a")},
	{(L"knife_gypsy_jackknife"), (L"\uE02a")},
	{(L"knife_outdoor"), (L"\uE02a")},
	{(L"knife_stiletto"), (L"\uE02a")},
	{(L"knife_widowmaker"), (L"\uE02a")},
	{(L"knife_skeleton"), (L"\uE02a")}
};

namespace Overlay
{
    // Detects primary monitor geometry, then calls Init → Run → Shutdown.
    // Blocks until the render loop exits. Returns false if Init failed.
    bool Start();

    inline ImFont* small_font{nullptr};
    inline ImFont* weapon_font{nullptr};
	inline ImDrawList* draw_list = nullptr;

    // Explicit geometry variant — use when you already know the target
    // window / monitor rect.
    bool Start(int x, int y, int w, int h);
}