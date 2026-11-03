#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>

namespace touniu_solver {

// Two independent 64-bit identities. `primary` buckets transposition entries;
// `verifier` rejects the (extremely rare) primary collision. Both are XOR
// combinations of domain-separated component tokens so a live position can
// maintain them incrementally: replaying a token cancels its contribution.
struct PositionIdentity {
    std::uint64_t primary = 0;
    std::uint64_t verifier = 0;

    friend auto operator==(const PositionIdentity&, const PositionIdentity&)
        -> bool = default;
};

namespace identity_tokens {

// Domains are fixed: a token that enters the identity through one domain can
// only be removed through the same domain.
inline constexpr std::uint64_t DomainNextPlayer = 1;
inline constexpr std::uint64_t DomainDirection = 2;
inline constexpr std::uint64_t DomainContract = 3;
inline constexpr std::uint64_t DomainTricksRemaining = 4;
inline constexpr std::uint64_t DomainSeed = 5;
inline constexpr std::uint64_t DomainDrawIndex = 6;
inline constexpr std::uint64_t DomainValidReverseCount = 7;
inline constexpr std::uint64_t DomainPendingBlock = 8;
inline constexpr std::uint64_t DomainSeatWins = 11;
inline constexpr std::uint64_t DomainHandSize = 12;
inline constexpr std::uint64_t DomainHandMembership = 13;
inline constexpr std::uint64_t DomainCatalogSize = 20;
inline constexpr std::uint64_t DomainCatalogCardBase = 100;
inline constexpr std::uint64_t DomainTrickSize = 30;
inline constexpr std::uint64_t DomainTrickBase = 31;

void combine(PositionIdentity& identity, std::uint64_t domain, std::uint64_t value);
void combine_card(PositionIdentity& identity, const Card& card);
void combine_hand_card(PositionIdentity& identity, std::uint64_t seat, CardId id);
void combine_trick_play(
    PositionIdentity& identity,
    std::uint64_t index,
    const TrickPlay& play);

// Components of a complete position, used to build the reference identity and
// to initialize a live position's maintained identity.
struct IdentityInput {
    Seat next_player = Seat::West;
    Direction direction = Direction::Forward;
    Contract contract = Contract::NoTrump;
    std::uint32_t tricks_remaining = 0;
    std::uint32_t seed = 0;
    std::uint64_t draw_index = 0;
    std::uint32_t valid_reverse_count = 0;
    std::optional<Seat> pending_block;
    const std::array<std::vector<CardId>, 4>* hands = nullptr;
    const std::array<std::uint32_t, 4>* tricks_won = nullptr;
    const std::vector<Card>* catalog = nullptr;
    const Trick* current_trick = nullptr;
};

auto build_identity(const IdentityInput& input) -> PositionIdentity;

}  // namespace identity_tokens

}  // namespace touniu_solver

