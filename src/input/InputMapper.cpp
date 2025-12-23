#include "input/InputMapper.h"

namespace {
// Win32 virtual-key codes we want for defaults, but without including <windows.h>
// (this input layer remains platform-agnostic).
constexpr std::uint32_t VK_SHIFT  = 0x10;
constexpr std::uint32_t VK_LSHIFT = 0xA0;
constexpr std::uint32_t VK_RSHIFT = 0xA1;

constexpr std::uint32_t VK_LEFT  = 0x25;
constexpr std::uint32_t VK_UP    = 0x26;
constexpr std::uint32_t VK_RIGHT = 0x27;
constexpr std::uint32_t VK_DOWN  = 0x28;
} // namespace

namespace colony::input {

InputMapper::InputMapper() noexcept
{
    SetDefaultBinds();
    ClearState();
}

void InputMapper::SetDefaultBinds() noexcept
{
    // Clear all binds.
    for (std::size_t i = 0; i < kActionCount; ++i)
    {
        m_bindCounts[i] = 0;
        m_binds[i].fill(0);
    }

    // Classic free-cam movement defaults + arrow key alternatives.
    AddBinding(Action::MoveForward,  static_cast<std::uint32_t>('W'));
    AddBinding(Action::MoveForward,  VK_UP);

    AddBinding(Action::MoveBackward, static_cast<std::uint32_t>('S'));
    AddBinding(Action::MoveBackward, VK_DOWN);

    AddBinding(Action::MoveLeft,     static_cast<std::uint32_t>('A'));
    AddBinding(Action::MoveLeft,     VK_LEFT);

    AddBinding(Action::MoveRight,    static_cast<std::uint32_t>('D'));
    AddBinding(Action::MoveRight,    VK_RIGHT);

    AddBinding(Action::MoveDown,     static_cast<std::uint32_t>('Q'));
    AddBinding(Action::MoveUp,       static_cast<std::uint32_t>('E'));

    // Speed boost modifier (either shift).
    AddBinding(Action::SpeedBoost, VK_SHIFT);
    AddBinding(Action::SpeedBoost, VK_LSHIFT);
    AddBinding(Action::SpeedBoost, VK_RSHIFT);

    RecomputeActionStatesNoEvents();
}

void InputMapper::ClearState() noexcept
{
    m_keysDown.reset();
    m_actionDown.fill(false);
    m_actionEventCount = 0;
}

void InputMapper::ClearBindings(Action action) noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return;

    m_binds[idx].fill(0);
    m_bindCounts[idx] = 0;

    RecomputeActionStatesNoEvents();
}

void InputMapper::AddBinding(Action action, std::uint32_t vk) noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return;

    if (vk >= kVkCount)
        return;

    const auto count = static_cast<std::size_t>(m_bindCounts[idx]);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (m_binds[idx][i] == vk)
            return; // already bound
    }

    if (count >= kMaxBindingsPerAction)
        return;

    m_binds[idx][count] = vk;
    m_bindCounts[idx] = static_cast<std::uint8_t>(count + 1);

    RecomputeActionStatesNoEvents();
}

void InputMapper::RemoveBinding(Action action, std::uint32_t vk) noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return;

    const auto count = static_cast<std::size_t>(m_bindCounts[idx]);
    std::size_t found = count;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (m_binds[idx][i] == vk)
        {
            found = i;
            break;
        }
    }

    if (found >= count)
        return;

    // Compact.
    for (std::size_t i = found + 1; i < count; ++i)
        m_binds[idx][i - 1] = m_binds[idx][i];
    m_binds[idx][count - 1] = 0;
    m_bindCounts[idx] = static_cast<std::uint8_t>(count - 1);

    RecomputeActionStatesNoEvents();
}

std::span<const std::uint32_t> InputMapper::Bindings(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return {};

    const auto count = static_cast<std::size_t>(m_bindCounts[idx]);
    return std::span<const std::uint32_t>(m_binds[idx].data(), count);
}

std::span<const ActionEvent> InputMapper::ActionEvents() const noexcept
{
    return std::span<const ActionEvent>(m_actionEvents.data(), m_actionEventCount);
}

void InputMapper::PushActionEvent(Action action, ActionEventType type) noexcept
{
    if (m_actionEventCount < kMaxActionEvents)
    {
        m_actionEvents[m_actionEventCount++] = ActionEvent{ action, type };
    }
    else
    {
        ++m_droppedActionEvents;
    }
}

bool InputMapper::ComputeActionDown(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return false;

    const auto count = static_cast<std::size_t>(m_bindCounts[idx]);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto vk = m_binds[idx][i];
        if (vk < kVkCount && vk != 0)
        {
            if (m_keysDown.test(static_cast<std::size_t>(vk)))
                return true;
        }
    }

    return false;
}

void InputMapper::RecomputeActionStatesNoEvents() noexcept
{
    for (std::size_t i = 0; i < kActionCount; ++i)
    {
        const auto a = static_cast<Action>(i);
        m_actionDown[i] = ComputeActionDown(a);
    }
}

void InputMapper::RefreshActionsAndEmitTransitions() noexcept
{
    for (std::size_t i = 0; i < kActionCount; ++i)
    {
        const auto a = static_cast<Action>(i);
        const bool newDown = ComputeActionDown(a);
        if (newDown != m_actionDown[i])
        {
            PushActionEvent(a, newDown ? ActionEventType::Pressed : ActionEventType::Released);
            m_actionDown[i] = newDown;
        }
    }
}

bool InputMapper::Consume(std::span<const InputEvent> events) noexcept
{
    m_actionEventCount = 0;

    for (const auto& ev : events)
    {
        bool recompute = false;

        switch (ev.type)
        {
        case InputEventType::KeyDown:
        {
            const auto vk = ev.key;
            if (vk < kVkCount)
            {
                const auto i = static_cast<std::size_t>(vk);
                const bool wasDown = m_keysDown.test(i);
                m_keysDown.set(i);
                // Ignore repeats; but still keep state sane.
                recompute = (!wasDown);
            }
            break;
        }

        case InputEventType::KeyUp:
        {
            const auto vk = ev.key;
            if (vk < kVkCount)
            {
                const auto i = static_cast<std::size_t>(vk);
                const bool wasDown = m_keysDown.test(i);
                m_keysDown.reset(i);
                recompute = wasDown;
            }
            break;
        }

        case InputEventType::FocusLost:
            // KeyUp may never be delivered once focus is gone; clear everything and
            // emit releases for any active actions.
            m_keysDown.reset();
            recompute = true;
            break;

        default:
            break;
        }

        if (recompute)
            RefreshActionsAndEmitTransitions();
    }

    return m_actionEventCount != 0;
}

bool InputMapper::IsDown(Action action) const noexcept
{
    const auto idx = ToIndex(action);
    if (idx >= kActionCount)
        return false;
    return m_actionDown[idx];
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
