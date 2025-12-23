#include "input/InputMapper.h"

namespace colony::input {

InputMapper::InputMapper() noexcept
{
    SetDefaultBinds();
}

void InputMapper::SetDefaultBinds() noexcept
{
    // Classic free-cam movement defaults.
    BindKey(Action::MoveForward,  static_cast<std::uint32_t>('W'));
    BindKey(Action::MoveBackward, static_cast<std::uint32_t>('S'));
    BindKey(Action::MoveLeft,     static_cast<std::uint32_t>('A'));
    BindKey(Action::MoveRight,    static_cast<std::uint32_t>('D'));
    BindKey(Action::MoveDown,     static_cast<std::uint32_t>('Q'));
    BindKey(Action::MoveUp,       static_cast<std::uint32_t>('E'));
}

void InputMapper::ClearState() noexcept
{
    m_keysDown.reset();
}

void InputMapper::BindKey(Action action, std::uint32_t vk) noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= m_binds.size())
        return;

    // Virtual-key codes are 0..255.
    if (vk >= kVkCount)
        vk = 0;

    m_binds[idx] = vk;
}

std::uint32_t InputMapper::BoundKey(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= m_binds.size())
        return 0;
    return m_binds[idx];
}

bool InputMapper::Consume(std::span<const InputEvent> events) noexcept
{
    bool changed = false;

    for (const auto& ev : events)
    {
        switch (ev.type)
        {
        case InputEventType::KeyDown:
        {
            const auto vk = ev.key;
            if (vk < kVkCount)
            {
                const bool wasDown = m_keysDown.test(static_cast<std::size_t>(vk));
                m_keysDown.set(static_cast<std::size_t>(vk));
                // Don't report repeats as changes.
                if (!wasDown && !ev.repeat)
                    changed = true;
            }
            break;
        }

        case InputEventType::KeyUp:
        {
            const auto vk = ev.key;
            if (vk < kVkCount)
            {
                const bool wasDown = m_keysDown.test(static_cast<std::size_t>(vk));
                m_keysDown.reset(static_cast<std::size_t>(vk));
                if (wasDown)
                    changed = true;
            }
            break;
        }

        case InputEventType::FocusLost:
        {
            if (m_keysDown.any())
                changed = true;
            m_keysDown.reset();
            break;
        }

        default:
            break;
        }
    }

    return changed;
}

bool InputMapper::IsDown(Action action) const noexcept
{
    const auto vk = BoundKey(action);
    if (vk == 0 || vk >= kVkCount)
        return false;

    return m_keysDown.test(static_cast<std::size_t>(vk));
}

MovementAxes InputMapper::GetMovementAxes() const noexcept
{
    MovementAxes a{};
    a.x = (IsDown(Action::MoveRight) ? 1.f : 0.f) - (IsDown(Action::MoveLeft) ? 1.f : 0.f);
    a.y = (IsDown(Action::MoveForward) ? 1.f : 0.f) - (IsDown(Action::MoveBackward) ? 1.f : 0.f);
    a.z = (IsDown(Action::MoveUp) ? 1.f : 0.f) - (IsDown(Action::MoveDown) ? 1.f : 0.f);
    return a;
}

} // namespace colony::input
