#pragma once
#include <vector>
#include <functional>
#include <entt/entt.hpp>

namespace ecs {

class CommandBuffer {
public:
    void clear() { cmds_.clear(); }

    template<class Fn>
    void push(Fn&& fn) {
        cmds_.emplace_back(std::forward<Fn>(fn));
    }

    // Convenience helpers
    void destroy(entt::entity e) {
        push([e](entt::registry& r){ if (r.valid(e)) r.destroy(e); });
    }

    template<class C, class... Args>
    void emplace(entt::entity e, Args&&... args) {
        push([=](entt::registry& r){ r.emplace_or_replace<C>(e, args...); });
    }

    template<class C>
    void remove(entt::entity e) {
        push([=](entt::registry& r){ if (r.any_of<C>(e)) r.remove<C>(e); });
    }

    void apply(entt::registry& r) {
        for (auto& fn : cmds_) fn(r);
        cmds_.clear();
    }
private:
    std::vector<std::function<void(entt::registry&)>> cmds_;
};

} // namespace ecs
