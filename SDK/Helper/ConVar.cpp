//
// ConVar.cpp  —  implementations.
// Utils.h is included here (not in ConVar.h) to break the circular dependency:
//   globals.h → ConVar.h is safe because ConVar.h has no Utils.h dependency.
//   ConVar.cpp → Utils.h → globals.h → ConVar.h (declarations only, no R() calls).
//

#include "ConVar.h"
#include "../../Utils/Utils.h"   // CUtils, R() — adjust path to match your tree

// ─────────────────────────────────────────────────────────────────────────────
//  get_convar
// ─────────────────────────────────────────────────────────────────────────────

std::optional<uintptr_t> GetConvar(
    uintptr_t        qw_convar_interface,
    std::string_view sz_name)
{
    if (!qw_convar_interface)
        return std::nullopt;

    const auto qw_objects =
        R().ReadMem<uintptr_t>(qw_convar_interface + convar_layout::k_object_list_offset);

    const auto dw_count =
        R().ReadMem<uint32_t>(qw_convar_interface + convar_layout::k_object_count_offset);

    for (uint32_t i = 0; i < dw_count; ++i)
    {
        const auto qw_object =
            R().ReadMem<uintptr_t>(qw_objects + i * convar_layout::k_object_stride);

        if (!qw_object)
            break;

        const auto   qw_name_ptr = R().ReadMem<uintptr_t>(qw_object);
        const std::string sz_entry    = R().ReadString(qw_name_ptr);

        if (sz_entry == sz_name)
            return qw_object;
    }

    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
//  resolve_convar_offsets
// ─────────────────────────────────────────────────────────────────────────────

bool ResolveConvarOffsets(Offsets& offsets, const std::string& sz_tier0_module)
{
    // ── VEngineCvar interface ─────────────────────────────────────────────────
    const uintptr_t qw_tier0_base = R().GetModuleBase(sz_tier0_module);
    if (!qw_tier0_base)
        return false;   // tier0 module not found

    const auto qw_cvar = R().GetInterfaceOffset(qw_tier0_base, "VEngineCvar0");
    if (!qw_cvar)
        return false;   // VEngineCvar0 interface not found

    offsets.iface.cvar = *qw_cvar;

    // ── mp_teammates_are_enemies  (FFA flag) ─────────────────────────────────
    const auto qw_ffa = GetConvar(offsets.iface.cvar, "mp_teammates_are_enemies");
    if (!qw_ffa)
        return false;   // mp_teammates_are_enemies convar not found

    offsets.convar.ffa = *qw_ffa;

    // ── sensitivity ──────────────────────────────────────────────────────────
    const auto qw_sens = GetConvar(offsets.iface.cvar, "sensitivity");
    if (!qw_sens)
        return false;   // sensitivity convar not found

    offsets.convar.sensitivity = *qw_sens;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Value accessors
// ─────────────────────────────────────────────────────────────────────────────

float GetSensitivity(const Offsets& offsets)
{
    return R().ReadMem<float>(offsets.convar.sensitivity + convar_layout::k_value_offset);
}

bool IsFFA(const Offsets& offsets)
{
    return R().ReadMem<uint8_t>(offsets.convar.ffa + convar_layout::k_value_offset) == 1u;
}