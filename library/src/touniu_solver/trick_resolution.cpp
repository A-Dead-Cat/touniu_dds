#include "trick_resolution.hpp"

#include <cassert>

namespace touniu_solver {
namespace {

auto next_seat(const Seat seat, const Direction direction) -> Seat
{
    const int value = static_cast<int>(seat);
    return static_cast<Seat>((value + (direction == Direction::Forward ? 1 : 3)) % 4);
}

auto contract_trump(const Contract contract) -> Suit
{
    return static_cast<Suit>(static_cast<int>(contract));
}

auto is_trump(const State& state, const Card& card, const Action& action) -> bool
{
    if (card.kind() == CardKind::BigJoker || card.kind() == CardKind::SmallJoker) {
        return true;
    }
    if (state.contract() == Contract::NoTrump) {
        return card.kind() == CardKind::NoTrump;
    }
    return action.effective_suit() == contract_trump(state.contract());
}

auto eligible(const State& state, const Trick& trick, const TrickPlay& play) -> int
{
    if (play.action.is_pass()) {
        return 0;
    }
    const Card* card = state.card(*play.action.card_id());
    assert(card != nullptr);
    if (trick.front().action.is_pass()) {
        return is_trump(state, *card, play.action) ? 3 : 1;
    }
    const Card* lead = state.card(*trick.front().action.card_id());
    assert(lead != nullptr);
    if (lead->kind() == CardKind::FourSuit) {
        return 1;
    }
    if (is_trump(state, *card, play.action)) {
        return 3;
    }
    if (lead->kind() == CardKind::ThreeSuitCat) {
        return 1;
    }
    return play.action.effective_suit() == trick.front().action.effective_suit() ? 2 : 0;
}

}  // namespace

auto resolve_trick(const State& state, const Trick& trick) -> TrickResult
{
    assert(trick.size() == 4U);
    std::size_t best = 0;
    int best_class = -1;
    std::int32_t best_rank = -1;
    for (std::size_t index = 0; index < trick.size(); ++index) {
        const TrickPlay& play = trick[index];
        const int card_class = eligible(state, trick, play);
        const Card* card = play.action.is_pass() ? nullptr : state.card(*play.action.card_id());
        const std::int32_t rank = card == nullptr ? -1 : card->rank().millionths();
        if (card_class > best_class || (card_class == best_class && rank > best_rank)) {
            best = index;
            best_class = card_class;
            best_rank = rank;
        }
    }

    TrickResult result{trick[best].seat, std::nullopt, 0, {}};
    const Card* winner = trick[best].action.is_pass()
        ? nullptr : state.card(*trick[best].action.card_id());
    if (winner != nullptr && winner->kind() == CardKind::CherryBomb) {
        result.winner = next_seat(trick[best].seat, state.direction());
    }

    for (std::size_t index = 0; index < trick.size(); ++index) {
        if (trick[index].action.is_pass()) {
            continue;
        }
        const Card* card = state.card(*trick[index].action.card_id());
        assert(card != nullptr);
        const bool valid = trick[index].action.mode() != PlayMode::Discard;
        if (card->kind() == CardKind::Block && valid && index < 3U) {
            result.forced_pass = trick[index + 1U].seat;
        } else if (card->kind() == CardKind::Reverse && valid) {
            ++result.valid_reverse_count;
        } else if (card->kind() == CardKind::PlusTwo && valid) {
            result.draws.push_back({next_seat(trick[index].seat, state.direction()), 2U});
        }
    }
    return result;
}

}  // namespace touniu_solver
