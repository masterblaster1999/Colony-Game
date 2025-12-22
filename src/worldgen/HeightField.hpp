// src/worldgen/HeightField.hpp
#pragma once

#include <cstddef>
#include <vector>

namespace cg
{
    // Row-major heightfield (y*W + x)
    struct HeightField
    {
        int w = 0;
        int h = 0;

        std::vector<float> data;

        HeightField() = default;

        // Matches the constructor behavior you already had in DomainWarp.hpp:
        // initializes to 0.f
        HeightField(int W, int H)
            : w(W)
            , h(H)
            , data(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0.0f)
        {}

        // Optional convenience (won't break existing call sites)
        HeightField(int W, int H, float initValue)
            : w(W)
            , h(H)
            , data(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), initValue)
        {}

        [[nodiscard]] bool valid() const noexcept
        {
            return w > 0 && h > 0 &&
                   data.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        }

        [[nodiscard]] std::size_t size() const noexcept { return data.size(); }

        inline float& at(int x, int y) noexcept
        {
            return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                        static_cast<std::size_t>(x)];
        }

        inline const float& at(int x, int y) const noexcept
        {
            return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                        static_cast<std::size_t>(x)];
        }
    };
} // namespace cg
