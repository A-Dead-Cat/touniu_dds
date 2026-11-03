#pragma once

#include <cstdint>

#include <touniu_solver/position.hpp>

namespace touniu_solver {

auto hash_position(const Position& position) -> std::uint64_t;

}  // namespace touniu_solver
