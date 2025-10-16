#pragma once
#include "Types.h"
#include "CommandBuffer.h"
#include "Events.h"
#include <entt/entt.hpp>

namespace ecs {

class World {
public:
    World();

    entt::registry& registry() { return reg_; }
    entt::dispatcher& dispatcher() { return disp_; }
    CommandBuffer& commands() { return cmd_; }

    // Context accessors (unique/singleton data for the frame)
    template<class T, class... Args>
    T& set_ctx(Args&&... args) { return reg_.ctx().emplace<T>(std::forward<Args>(args)...); }

    template<class T>
    T& ctx() { return reg_.ctx().get<T>(); }

    // Frame lifecycle
    void begin_frame();
    void end_frame();

private:
    entt::registry reg_;
    entt::dispatcher disp_;
    CommandBuffer cmd_;
    std::uint64_t frameIndex_{0};
};

} // namespace ecs
