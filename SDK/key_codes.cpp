#include "key_codes.h"

std::string_view key_code_to_string(CS2KeyCode e_key) noexcept
{
    switch (e_key)
    {
        case CS2KeyCode::Num0:          return "Key0";
        case CS2KeyCode::Num1:          return "Key1";
        case CS2KeyCode::Num2:          return "Key2";
        case CS2KeyCode::Num3:          return "Key3";
        case CS2KeyCode::Num4:          return "Key4";
        case CS2KeyCode::Num5:          return "Key5";
        case CS2KeyCode::Num6:          return "Key6";
        case CS2KeyCode::Num7:          return "Key7";
        case CS2KeyCode::Num8:          return "Key8";
        case CS2KeyCode::Num9:          return "Key9";
        case CS2KeyCode::A:             return "KeyA";
        case CS2KeyCode::B:             return "KeyB";
        case CS2KeyCode::C:             return "KeyC";
        case CS2KeyCode::D:             return "KeyD";
        case CS2KeyCode::E:             return "KeyE";
        case CS2KeyCode::F:             return "KeyF";
        case CS2KeyCode::G:             return "KeyG";
        case CS2KeyCode::H:             return "KeyH";
        case CS2KeyCode::I:             return "KeyI";
        case CS2KeyCode::J:             return "KeyJ";
        case CS2KeyCode::K:             return "KeyK";
        case CS2KeyCode::L:             return "KeyL";
        case CS2KeyCode::M:             return "KeyM";
        case CS2KeyCode::N:             return "KeyN";
        case CS2KeyCode::O:             return "KeyO";
        case CS2KeyCode::P:             return "KeyP";
        case CS2KeyCode::Q:             return "KeyQ";
        case CS2KeyCode::R:             return "KeyR";
        case CS2KeyCode::S:             return "KeyS";
        case CS2KeyCode::T:             return "KeyT";
        case CS2KeyCode::U:             return "KeyU";
        case CS2KeyCode::V:             return "KeyV";
        case CS2KeyCode::W:             return "KeyW";
        case CS2KeyCode::X:             return "KeyX";
        case CS2KeyCode::Y:             return "KeyY";
        case CS2KeyCode::Z:             return "KeyZ";
        case CS2KeyCode::Space:         return "Space";
        case CS2KeyCode::Backspace:     return "Backspace";
        case CS2KeyCode::Tab:           return "Tab";
        case CS2KeyCode::Escape:        return "Escape";
        case CS2KeyCode::Insert:        return "Insert";
        case CS2KeyCode::Delete:        return "Delete";
        case CS2KeyCode::Home:          return "Home";
        case CS2KeyCode::End:           return "End";
        case CS2KeyCode::MouseLeft:     return "MouseLeft";
        case CS2KeyCode::MouseRight:    return "MouseRight";
        case CS2KeyCode::MouseMiddle:   return "MouseMiddle";
        case CS2KeyCode::Mouse4:        return "Mouse4";
        case CS2KeyCode::Mouse5:        return "Mouse5";
        case CS2KeyCode::MouseWheelUp:  return "MouseWheelUp";
        case CS2KeyCode::MouseWheelDown:return "MouseWheelDown";
        default:                     return "Unknown";
    }
}

std::optional<CS2KeyCode> key_code_from_string(std::string_view sv_input) noexcept
{
    if (sv_input == "Key0")           return CS2KeyCode::Num0;
    if (sv_input == "Key1")           return CS2KeyCode::Num1;
    if (sv_input == "Key2")           return CS2KeyCode::Num2;
    if (sv_input == "Key3")           return CS2KeyCode::Num3;
    if (sv_input == "Key4")           return CS2KeyCode::Num4;
    if (sv_input == "Key5")           return CS2KeyCode::Num5;
    if (sv_input == "Key6")           return CS2KeyCode::Num6;
    if (sv_input == "Key7")           return CS2KeyCode::Num7;
    if (sv_input == "Key8")           return CS2KeyCode::Num8;
    if (sv_input == "Key9")           return CS2KeyCode::Num9;
    if (sv_input == "KeyA")           return CS2KeyCode::A;
    if (sv_input == "KeyB")           return CS2KeyCode::B;
    if (sv_input == "KeyC")           return CS2KeyCode::C;
    if (sv_input == "KeyD")           return CS2KeyCode::D;
    if (sv_input == "KeyE")           return CS2KeyCode::E;
    if (sv_input == "KeyF")           return CS2KeyCode::F;
    if (sv_input == "KeyG")           return CS2KeyCode::G;
    if (sv_input == "KeyH")           return CS2KeyCode::H;
    if (sv_input == "KeyI")           return CS2KeyCode::I;
    if (sv_input == "KeyJ")           return CS2KeyCode::J;
    if (sv_input == "KeyK")           return CS2KeyCode::K;
    if (sv_input == "KeyL")           return CS2KeyCode::L;
    if (sv_input == "KeyM")           return CS2KeyCode::M;
    if (sv_input == "KeyN")           return CS2KeyCode::N;
    if (sv_input == "KeyO")           return CS2KeyCode::O;
    if (sv_input == "KeyP")           return CS2KeyCode::P;
    if (sv_input == "KeyQ")           return CS2KeyCode::Q;
    if (sv_input == "KeyR")           return CS2KeyCode::R;
    if (sv_input == "KeyS")           return CS2KeyCode::S;
    if (sv_input == "KeyT")           return CS2KeyCode::T;
    if (sv_input == "KeyU")           return CS2KeyCode::U;
    if (sv_input == "KeyV")           return CS2KeyCode::V;
    if (sv_input == "KeyW")           return CS2KeyCode::W;
    if (sv_input == "KeyX")           return CS2KeyCode::X;
    if (sv_input == "KeyY")           return CS2KeyCode::Y;
    if (sv_input == "KeyZ")           return CS2KeyCode::Z;
    if (sv_input == "Space")          return CS2KeyCode::Space;
    if (sv_input == "Backspace")      return CS2KeyCode::Backspace;
    if (sv_input == "Tab")            return CS2KeyCode::Tab;
    if (sv_input == "Escape")         return CS2KeyCode::Escape;
    if (sv_input == "Insert")         return CS2KeyCode::Insert;
    if (sv_input == "Delete")         return CS2KeyCode::Delete;
    if (sv_input == "Home")           return CS2KeyCode::Home;
    if (sv_input == "End")            return CS2KeyCode::End;
    if (sv_input == "MouseLeft")      return CS2KeyCode::MouseLeft;
    if (sv_input == "MouseRight")     return CS2KeyCode::MouseRight;
    if (sv_input == "MouseMiddle")    return CS2KeyCode::MouseMiddle;
    if (sv_input == "Mouse4")         return CS2KeyCode::Mouse4;
    if (sv_input == "Mouse5")         return CS2KeyCode::Mouse5;
    if (sv_input == "MouseWheelUp")   return CS2KeyCode::MouseWheelUp;
    if (sv_input == "MouseWheelDown") return CS2KeyCode::MouseWheelDown;

    return std::nullopt;
}