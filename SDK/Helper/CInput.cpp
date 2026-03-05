#include "CInput.h"

void Input::update(const read_fn& fn_read, std::uint64_t u64_button_base) noexcept
{
    std::uint8_t a_raw_buf[KeyState::k_u64ByteSize]{};

    if (!fn_read(u64_button_base, a_raw_buf, sizeof(a_raw_buf)))
        return;

    std::swap(m_previous_state, m_current_state);
    m_current_state.load(a_raw_buf);
}

bool Input::is_key_pressed(CS2KeyCode e_key) const noexcept
{
    return m_current_state.get(static_cast<std::uint32_t>(e_key));
}

bool Input::key_just_pressed(CS2KeyCode e_key) const noexcept
{
    const auto u32_idx = static_cast<std::uint32_t>(e_key);
    return !m_previous_state.get(u32_idx) && m_current_state.get(u32_idx);
}