// include/game/EventScheduler.h
#pragma once
#include "EventSystem.h"
#include "Rng.h"      // your RNG wrapper
#include "World.h"    // or forward declare + pass some query interface

class EventScheduler
{
public:
    explicit EventScheduler(uint64_t seed);

    void Tick(double dt, const Colony& colony, Rng& rng, EventSystem& events);

    // For save/load
    void SetGameTime(double t) { t_ = t; }
    double GameTime() const { return t_; }

private:
    double t_ = 0.0;
    double nextStormTime_ = 0.0;
    double nextMeteorTime_ = 0.0;

    void ScheduleNextStorm(Rng& rng, const Colony& colony);
    void ScheduleNextMeteor(Rng& rng, const Colony& colony);
};
