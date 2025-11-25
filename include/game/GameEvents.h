// include/game/GameEvents.h
#pragma once
#include "ResearchIds.h"
#include <string>
#include <variant>

enum class EventKind : uint8_t
{
    ResearchCompleted,
    DustStormStarted,
    DustStormEnded,
    MeteorStrike,
    Message
};

struct ResearchCompletedEvent {
    ResearchId id;
};

struct DustStormEvent {
    bool started;     // true = began, false = ended
};

struct MeteorStrikeEvent {
    int x, y;         // tile coords
    int damageRadius;
};

struct MessageEvent {
    std::string text; // generic UI text
};

using EventPayload = std::variant<
    ResearchCompletedEvent,
    DustStormEvent,
    MeteorStrikeEvent,
    MessageEvent
>;

struct GameEvent
{
    EventKind   kind;
    double      gameTime; // seconds since start
    EventPayload payload;
};
