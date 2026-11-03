#include "position_hash.hpp"

#include <algorithm>
#include <cstdint>

namespace touniu_solver {
namespace {

constexpr std::uint64_t PrimarySeed = 0x243F6A8885A308D3ULL;
constexpr std::uint64_t VerifierSeed = 0x13198A2E03707344ULL;
constexpr std::uint64_t TokenIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t TokenMultiplierOne = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t TokenMultiplierTwo = 0x94D049BB133111EBULL;

// Domain-separated component token. XORing tokens makes catalog and hand
// membership order-independent while trick slots stay ordered through their
// explicit index domain.
auto token(
    const std::uint64_t seed,
    const std::uint64_t domain,
    const std::uint64_t value) -> std::uint64_t
{
    std::uint64_t mixed = seed + domain * TokenIncrement + value;
    mixed = (mixed ^ (mixed >> 30U)) * TokenMultiplierOne;
    mixed = (mixed ^ (mixed >> 27U)) * TokenMultiplierTwo;
    return mixed ^ (mixed >> 31U);
}

void combine(
    PositionIdentity& identity,
    const std::uint64_t domain,
    const std::uint64_t value)
{
    identity.primary ^= token(PrimarySeed, domain, value);
    identity.verifier ^= token(VerifierSeed, domain, value);
}

void combine_card(PositionIdentity& identity, const Card& card)
{
    combine(identity, 100U, card.id().value);
    combine(identity, 101U, static_cast<std::uint64_t>(card.kind()));
    combine(identity, 102U, static_cast<std::uint64_t>(card.rank().millionths()));
    combine(identity, 103U, card.intrinsic_suits().size());
    for (const Suit suit : card.intrinsic_suits()) {
        combine(identity, 104U, static_cast<std::uint64_t>(suit));
    }
}

}  // namespace

auto recompute_position_identity(const Position& position) -> PositionIdentity
{
    PositionIdentity identity;

    combine(identity, 1U, static_cast<std::uint64_t>(position.next_player()));
    combine(identity, 2U, static_cast<std::uint64_t>(position.direction()));
    combine(identity, 3U, static_cast<std::uint64_t>(position.contract()));
    combine(identity, 4U, position.tricks_remaining());
    combine(identity, 5U, position.seed());
    combine(identity, 6U, position.draw_index());
    combine(identity, 7U, position.valid_reverse_count());
    combine(identity, 8U, position.pending_block().has_value()
        ? static_cast<std::uint64_t>(*position.pending_block()) + 1U : 0U);

    for (int seat_value = 0; seat_value < 4; ++seat_value) {
        const Seat seat = static_cast<Seat>(seat_value);
        combine(identity, 10U, seat_value);
        combine(identity, 11U, position.tricks_won(seat));
        const std::vector<CardId>& hand = position.hand(seat);
        combine(identity, 12U, hand.size());
        for (const CardId id : hand) {
            combine(identity, 13U, static_cast<std::uint64_t>(seat_value) << 32U | id.value);
            // Membership must include the card definition as well, so a catalog
            // change with an unchanged ID still changes the identity.
        }
    }

    // The complete catalog participates, including cards no longer in any hand
    // (for example a floating card already played in a collected trick). This
    // keeps future +2 generation and the transposition key aligned.
    combine(identity, 20U, position.card_catalog().size());
    for (const Card& card : position.card_catalog()) {
        combine_card(identity, card);
    }

    combine(identity, 30U, position.current_trick().size());
    std::uint64_t index = 0;
    for (const TrickPlay& play : position.current_trick()) {
        combine(identity, 31U, index * 8U + static_cast<std::uint64_t>(play.seat));
        combine(identity, 32U, play.action.is_pass() ? 0U : 1U);
        if (!play.action.is_pass()) {
            combine(identity, 33U, play.action.card_id()->value);
            combine(identity, 34U, static_cast<std::uint64_t>(*play.action.mode()));
            combine(identity, 35U, static_cast<std::uint64_t>(*play.action.effective_suit()));
        }
        ++index;
    }

    return identity;
}

auto hash_position(const Position& position) -> std::uint64_t
{
    return recompute_position_identity(position).primary;
}

}  // namespace touniu_solver
