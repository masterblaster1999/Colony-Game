#include "doctest.h"

#include "input/InputMapper.h"
#include "input/InputBindingParse.h"

TEST_CASE("InputMapper: generic Ctrl/Shift/Alt modifiers match left/right variants")
{
    using colony::input::Action;
    using colony::input::ActionEventType;
    using colony::input::InputEvent;
    using colony::input::InputEventType;
    namespace bindings = colony::input::bindings;

    colony::input::InputMapper m;

    // Bind SaveWorld to a generic chord: Ctrl + S
    m.ClearBindings(Action::SaveWorld);
    {
        const std::uint32_t chord[] = { bindings::kVK_CONTROL, static_cast<std::uint32_t>('S') };
        m.AddBinding(Action::SaveWorld, std::span<const std::uint32_t>(chord, 2));
    }

    m.ClearState();
    m.BeginFrame();

    // Press LEFT ctrl (Win32 commonly reports L/R ctrl rather than VK_CONTROL).
    m.ConsumeEvent(InputEvent{ InputEventType::KeyDown, bindings::kVK_LCONTROL });
    CHECK_FALSE(m.IsDown(Action::SaveWorld));

    // Press 'S'
    m.ConsumeEvent(InputEvent{ InputEventType::KeyDown, static_cast<std::uint32_t>('S') });
    CHECK(m.IsDown(Action::SaveWorld));

    // We should have emitted a Pressed event for SaveWorld.
    bool sawPressed = false;
    for (const auto& e : m.ActionEvents())
    {
        if (e.action == Action::SaveWorld && e.type == ActionEventType::Pressed)
        {
            sawPressed = true;
            break;
        }
    }
    CHECK(sawPressed);

    // Release 'S' (should release the action).
    m.ConsumeEvent(InputEvent{ InputEventType::KeyUp, static_cast<std::uint32_t>('S') });
    CHECK_FALSE(m.IsDown(Action::SaveWorld));

    // Release LEFT ctrl.
    m.ConsumeEvent(InputEvent{ InputEventType::KeyUp, bindings::kVK_LCONTROL });
    CHECK_FALSE(m.IsDown(Action::SaveWorld));
}
