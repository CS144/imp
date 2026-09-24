#pragma once

#include <ranges>
#include <type_traits>

// This concept represents any range (e.g. vector) whose elements can be converted to string_view.
template<typename R>
concept StringViewRange
  = std::ranges::sized_range<R> && std::is_constructible_v<std::string_view, std::ranges::range_value_t<R>>;
