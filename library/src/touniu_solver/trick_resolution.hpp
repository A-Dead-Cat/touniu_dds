#pragma once

#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>

namespace touniu_solver {

struct DrawEffect {
    Seat seat;
    std::uint32_t count;
};

struct TrickResult {
    Seat winner;
    std::optional<Seat> forced_pass;
    std::uint32_t valid_reverse_count = 0;
    std::vector<DrawEffect> draws;
};

auto resolve_trick(const State& state, const Trick& trick) -> TrickResult;

}  // namespace touniu_solver
