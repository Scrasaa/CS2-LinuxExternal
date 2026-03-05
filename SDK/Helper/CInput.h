#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include "SDK/key_codes.h"

// ============================================================
//  KeyState — a flat 512-bit bitset mirroring CS2's internal
//  IInputSystem button state buffer (64 bytes).
// ============================================================
class KeyState
{
public:
    static constexpr std::uint64_t k_u64MaxKeys  = 512;
    static constexpr std::uint64_t k_u64ByteSize = k_u64MaxKeys / 8;  // 64

    [[nodiscard]] bool get(std::uint32_t u32_index) const noexcept
    {
        if (u32_index >= k_u64MaxKeys)
            return false;

        return (m_aBytes[u32_index / 8] >> (u32_index % 8)) & 1u;
    }

    void load(const std::uint8_t* p_src) noexcept
    {
        std::copy_n(p_src, k_u64ByteSize, m_aBytes.data());
    }

    void clear() noexcept { m_aBytes.fill(0); }

private:
    std::array<std::uint8_t, k_u64ByteSize> m_aBytes{};
};


// ============================================================
//  Input — wraps the two-frame (previous / current) key state
//  read from CS2's InputSystemVersion0 interface.
//
//  How to resolve the addresses (from find_offsets equivalent):
//
//   1. Resolve the InputSystemVersion0 interface base:
//         u64InputBase = get_interface_offset(inputsystem_lib, "InputSystemVersion0");
//
//   2. Resolve the button_state sub-offset:
//         // vtable slot 19 holds IsButtonDown or similar; +0x14 inside
//         // that function holds the RVA of the key buffer relative to
//         // the interface object.
//         u64VtFn       = read_vtable_fn(u64InputBase, 19);
//         u32BtnOffset  = read<uint32_t>(u64VtFn + 0x14);
//         u64ButtonBase = u64InputBase + u32BtnOffset;   // pass this to update()
//
//   3. Call update() every tick with u64ButtonBase.
// ============================================================
class Input
{
public:
    // Signature: fn(u64 address, void* dst, size_t bytes) -> bool
    using read_fn = std::function<bool(std::uint64_t, void*, std::size_t)>;

    Input() = default;

    // Reads the 64-byte key state buffer from the game process.
    // u64_button_base = resolved input_interface + button_state_offset (see above).
    void update(const read_fn& fn_read, std::uint64_t u64_button_base) noexcept;

    // True while the key is held this frame.
    [[nodiscard]] bool is_key_pressed(CS2KeyCode e_key) const noexcept;

    // True only on the exact frame the key transitioned from up → down.
    [[nodiscard]] bool key_just_pressed(CS2KeyCode e_key) const noexcept;

private:
    KeyState m_previous_state{};
    KeyState m_current_state{};
};