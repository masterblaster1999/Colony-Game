#pragma once

#include <cstdint>

namespace colony::jobs
{
    // Simple integer IDs so we don't depend on your existing types.
    using AgentId = std::uint32_t;
    using JobId   = std::uint32_t;

    constexpr JobId InvalidJobId = 0;

    // Basic 2D grid coordinate. You can replace this with your own Int2 later.
    struct Int2
    {
        int x = 0;
        int y = 0;
    };

    // Expand this with whatever job types you need.
    enum class JobType : std::uint8_t
    {
        None = 0,
        Mine,
        Haul,
        Build,
        Eat,
        Sleep,
        // Add more as needed...
    };

    // State machine for a job's lifecycle.
    enum class JobState : std::uint8_t
    {
        Open = 0,      // Available to take
        Reserved,      // Reserved by a specific agent, not yet started
        InProgress,    // Actively being worked on
        Completed,     // Finished successfully
        Cancelled      // Cancelled / invalid
    };

    // Core job data.
    struct Job
    {
        JobId   id           = InvalidJobId;
        JobType type         = JobType::None;
        JobState state       = JobState::Open;

        int     priority     = 0;       // Higher = more important
        Int2    targetTile{};           // Tile to go to / act on
        AgentId assignedAgent = 0;      // 0 = no agent
        // Later: resource type, quantity, building ID, etc.
    };

} // namespace colony::jobs
