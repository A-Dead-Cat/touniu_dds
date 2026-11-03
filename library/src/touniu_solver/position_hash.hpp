#pragma once

#include <cstdint>

#include <touniu_solver/position.hpp>

namespace touniu_solver {

// Two independent 64-bit identities. `primary` buckets transposition entries;
// `verifier` rejects the (extremely rare) primary collision. Both are XOR
// combinations of domain-separated component tokens so Phase 2b can maintain
// them incrementally.
struct PositionIdentity {
    std::uint64_t primary = 0;
    std::uint64_t verifier = 0;

    friend auto operator==(const PositionIdentity&, const PositionIdentity&) -> bool = default;
};

auto recompute_position_identity(const Position& position) -> PositionIdentity;

// Compatibility wrapper: the legacy single hash is the primary identity.
auto hash_position(const Position& position) -> std::uint64_t;

}  // namespace touniu_solver
