// include/game/EventSystem.h
#pragma once
#include "GameEvents.h"
#include <deque>
#include <functional>
#include <vector>

class EventSystem
{
public:
    using Handler = std::function<void(const GameEvent&)>;

    void Push(GameEvent&& e) {
        queue_.push_back(std::move(e));
    }

    // Convenience helpers:
    void PushResearchCompleted(double t, ResearchId id);
    void PushDustStormStarted(double t);
    void PushDustStormEnded(double t);
    void PushMessage(double t, std::string text);

    // Dispatch everything in the queue to registered handlers
    void DispatchAll();

    // Handlers are called in registration order
    void AddHandler(Handler h) {
        handlers_.push_back(std::move(h));
    }

private:
    std::deque<GameEvent> queue_;
    std::vector<Handler>  handlers_;
};
