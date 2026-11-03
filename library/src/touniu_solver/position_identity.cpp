#include "position_identity.hpp"

namespace touniu_solver {
namespace identity_tokens {
namespace {

constexpr std::uint64_t PrimarySeed = 0x243F6A8885A308D3ULL;
constexpr std::uint64_t VerifierSeed = 0x13198A2E03707344ULL;
constexpr std::uint64_t TokenIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t TokenMultiplierOne = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t TokenMultiplierTwo = 0x94D049BB133111EBULL;

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

}  // namespace

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
    combine(identity, DomainCatalogCardBase, card.id().value);
    combine(identity, DomainCatalogCardBase + 1U, static_cast<std::uint64_t>(card.kind()));
    combine(
        identity,
        DomainCatalogCardBase + 2U,
        static_cast<std::uint64_t>(card.rank().millionths()));
    combine(
        identity,
        DomainCatalogCardBase + 3U,
        card.intrinsic_suits().size());
    for (const Suit suit : card.intrinsic_suits()) {
        combine(identity, DomainCatalogCardBase + 4U, static_cast<std::uint64_t>(suit));
    }
}

void combine_hand_card(
    PositionIdentity& identity,
    const std::uint64_t seat,
    const CardId id)
{
    combine(identity, DomainHandMembership, seat << 32U | id.value);
}

void combine_trick_play(
    PositionIdentity& identity,
    const std::uint64_t index,
    const TrickPlay& play)
{
    combine(
        identity,
        DomainTrickBase,
        index * 8U + static_cast<std::uint64_t>(play.seat));
    combine(
        identity,
        DomainTrickBase + 1U,
        play.action.is_pass() ? 0U : 1U);
    if (!play.action.is_pass()) {
        combine(identity, DomainTrickBase + 2U, play.action.card_id()->value);
        combine(
            identity,
            DomainTrickBase + 3U,
            static_cast<std::uint64_t>(*play.action.mode()));
        combine(
            identity,
            DomainTrickBase + 4U,
            static_cast<std::uint64_t>(*play.action.effective_suit()));
    }
}

auto build_identity(const IdentityInput& input) -> PositionIdentity
{
    PositionIdentity identity;

    combine(identity, DomainNextPlayer, static_cast<std::uint64_t>(input.next_player));
    combine(identity, DomainDirection, static_cast<std::uint64_t>(input.direction));
    combine(identity, DomainContract, static_cast<std::uint64_t>(input.contract));
    combine(identity, DomainTricksRemaining, input.tricks_remaining);
    combine(identity, DomainSeed, input.seed);
    combine(identity, DomainDrawIndex, input.draw_index);
    combine(identity, DomainValidReverseCount, input.valid_reverse_count);
    combine(identity, DomainPendingBlock, input.pending_block.has_value()
        ? static_cast<std::uint64_t>(*input.pending_block) + 1U : 0U);

    for (int seat_value = 0; seat_value < 4; ++seat_value) {
        const std::uint64_t seat_bits = static_cast<std::uint64_t>(seat_value) << 32U;
        combine(
            identity,
            DomainSeatWins,
            seat_bits | (*input.tricks_won)[static_cast<std::size_t>(seat_value)]);
        const std::vector<CardId>& hand =
            (*input.hands)[static_cast<std::size_t>(seat_value)];
        combine(identity, DomainHandSize, seat_bits | hand.size());
        for (const CardId id : hand) {
            combine_hand_card(identity, static_cast<std::uint64_t>(seat_value), id);
        }
    }

    // The complete catalog participates, including cards no longer in any hand
    // (for example a floating card already played in a collected trick). This
    // keeps future +2 generation and the transposition key aligned.
    combine(identity, DomainCatalogSize, input.catalog->size());
    for (const Card& card : *input.catalog) {
        combine_card(identity, card);
    }

    combine(identity, DomainTrickSize, input.current_trick->size());
    std::uint64_t index = 0;
    for (const TrickPlay& play : *input.current_trick) {
        combine_trick_play(identity, index, play);
        ++index;
    }

    return identity;
}

}  // namespace identity_tokens
}  // namespace touniu_solver
