#pragma once

#include <ranges>

namespace Util {

struct enumerate_fn : std::ranges::range_adaptor_closure<enumerate_fn> {
    template<std::ranges::viewable_range R>
    constexpr auto operator()(R &&r) const
    {
        return std::views::zip(
            std::views::iota(std::ranges::range_difference_t<R> { 0 }),
            std::forward<R>(r));
    }
};
inline constexpr enumerate_fn enumerate { };

}
