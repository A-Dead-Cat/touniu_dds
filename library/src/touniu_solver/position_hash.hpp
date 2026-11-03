#pragma once

#include <cstdint>

#include <touniu_solver/position.hpp>
#include <touniu_solver/position_identity.hpp>

namespace touniu_solver {

auto recompute_position_identity(const Position& position) -> PositionIdentity;

// Compatibility wrapper: the legacy single hash is the primary identity.
auto hash_position(const Position& position) -> std::uint64_t;

}  // namespace touniu_solver
