#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// CS2 InputSystem key indices — match IInputSystem internal button bitset layout.
// Values sourced from InputSystemVersion0 interface (button_state bitset offsets).
enum class CS2KeyCode : std::uint32_t
{
    Num0 = 1,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Space      = 66,
    Backspace,
    Tab,
    Escape     = 71,
    Insert     = 73,
    Delete,
    Home,
    End,

    MouseLeft       = 317,
    MouseRight,
    MouseMiddle,
    Mouse4,
    Mouse5,
    MouseWheelUp,
    MouseWheelDown,
};

// Returns the string name of a key, or "Unknown" if unrecognised.
[[nodiscard]] std::string_view key_code_to_string(CS2KeyCode e_key) noexcept;

// Parses a key name string (e.g. "KeyA", "MouseLeft") into a KeyCode.
// Returns std::nullopt if the name is not recognised.
[[nodiscard]] std::optional<CS2KeyCode> key_code_from_string(std::string_view sv_input) noexcept;