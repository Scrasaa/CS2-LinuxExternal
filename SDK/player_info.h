#pragma once

#include <cstdint>
#include <string>

// ── EPlayerFlags ──────────────────────────────────────────────
//
//  Bit-field describing transient per-frame player state.
//  Stored inside PlayerInfo::flags.

enum EPlayerFlags : uint8_t
{
    FLAG_DEFUSING    = 1 << 0,
    FLAG_PLANTING    = 1 << 1,
    FLAG_SCOPED      = 1 << 2,
    FLAG_FLASHED     = 1 << 3,
    FLAG_HAS_C4      = 1 << 4,
    FLAG_HAS_DEFUSER = 1 << 5,
    FLAG_HAS_HELMET  = 1 << 6,
};

// ── PlayerInfo ────────────────────────────────────────────────
//
//  All game-state fields for one player, read once per frame in
//  CEntityCache::refresh() and consumed read-only in CESP::Run().
//
//  Visual / positional data (bones, screen bounds) is intentionally
//  excluded — those are render-side concerns owned by CESP.

struct PlayerInfo
{
    std::string szName             { "Unknown" };
    std::string szActiveWeaponName { "Unknown" };

    int32_t iWeaponDef     {};

    int32_t iHealth        {};
    int32_t iMaxHealth     {};
    int32_t iArmor         {};
    int32_t iTeamNum       {};
    int32_t iReserveAmmo   {};
    int32_t iClipPrimary   {};

    uint8_t bLifeState     {};   // 0 = alive
    uint8_t flags          {};

    // ── Flag helpers ──────────────────────────────────────────

    [[nodiscard]] bool HasFlag(EPlayerFlags f) const noexcept
    {
        return (flags & static_cast<uint8_t>(f)) != 0;
    }

    void SetFlag(EPlayerFlags f) noexcept
    {
        flags |= static_cast<uint8_t>(f);
    }

    void ClearFlag(EPlayerFlags f) noexcept
    {
        flags &= ~static_cast<uint8_t>(f);
    }
};