#pragma once

//
// ConVar.h  —  structs, layout constants, and function declarations.
// No dependency on Utils.h / R() — safe to include from globals.h.
//

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
//  Source 2  VEngineCvar  —  memory layout constants
// ─────────────────────────────────────────────────────────────────────────────

namespace convar_layout
{
    inline constexpr uintptr_t k_object_list_offset  = 0x48;
    inline constexpr uintptr_t k_object_count_offset = 160;
    inline constexpr uintptr_t k_object_stride       = 16;
    inline constexpr uintptr_t k_value_offset        = 0x58;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Offset tables
// ─────────────────────────────────────────────────────────────────────────────

struct ConvarOffsets
{
    uintptr_t sensitivity = 0;
    uintptr_t ffa         = 0;
};

struct InterfaceOffsets
{
    uintptr_t cvar = 0;
};

struct Offsets
{
    ConvarOffsets    convar;
    InterfaceOffsets iface;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Function declarations  (implemented in ConVar.cpp)
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] std::optional<uintptr_t> get_convar(
    uintptr_t        qw_convar_interface,
    std::string_view sz_name);

[[nodiscard]] bool resolve_convar_offsets(
    Offsets&           offsets,
    const std::string& sz_tier0_module);

[[nodiscard]] float   get_sensitivity(const Offsets& offsets);
[[nodiscard]] bool    is_ffa(const Offsets& offsets);