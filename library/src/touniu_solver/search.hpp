#pragma once

#include <optional>

#include <touniu_solver/position.hpp>

namespace touniu_solver {

enum class Partnership {
    NorthSouth,
    EastWest,
};

struct SearchResult {
    int tricks = 0;
    std::optional<Action> best_action;
};

auto solve(Position& position, Partnership target, bool use_alpha_beta = true)
    -> SearchResult;

}  // namespace touniu_solver
