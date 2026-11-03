#include "legal_moves.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace touniu_solver {
namespace {

auto has_ordinary_card(const State& state, const std::vector<CardId>& hand) -> bool
{
    return std::any_of(hand.begin(), hand.end(), [&state](const CardId id) {
        const Card* card = state.card(id);
        assert(card != nullptr);
        return card->kind() != CardKind::FourSuit &&
               card->kind() != CardKind::ThreeSuitCat;
    });
}

auto is_multi_suit_card(const Card& card) -> bool
{
    return card.kind() == CardKind::FourSuit ||
           card.kind() == CardKind::ThreeSuitCat;
}

auto trump_suit(const Contract contract) -> std::optional<Suit>
{
    switch (contract) {
        case Contract::Spades:
            return Suit::Spades;
        case Contract::Hearts:
            return Suit::Hearts;
        case Contract::Diamonds:
            return Suit::Diamonds;
        case Contract::Clubs:
            return Suit::Clubs;
        case Contract::NoTrump:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

Action::Action(
    std::optional<CardId> card_id,
    std::optional<PlayMode> mode,
    std::optional<Suit> effective_suit)
    : card_id_(card_id),
      mode_(mode),
      effective_suit_(effective_suit)
{
}

auto Action::play(const CardId card_id, const PlayMode mode, const Suit effective_suit)
    -> Action
{
    return Action(card_id, mode, effective_suit);
}

auto Action::pass() -> Action
{
    return Action(std::nullopt, std::nullopt, std::nullopt);
}

auto Action::is_pass() const -> bool
{
    return !card_id_.has_value();
}

auto Action::card_id() const -> std::optional<CardId>
{
    return card_id_;
}

auto Action::mode() const -> std::optional<PlayMode>
{
    return mode_;
}

auto Action::effective_suit() const -> std::optional<Suit>
{
    return effective_suit_;
}

auto legal_actions(const State& state, const Trick& trick) -> std::vector<Action>
{
    const std::vector<CardId>& hand = state.hand(state.next_player());
    if (hand.empty()) {
        return {Action::pass()};
    }

    std::vector<Action> actions;
    if (trick.empty()) {
        const bool ordinary_card_remains = has_ordinary_card(state, hand);
        for (const CardId id : hand) {
            const Card* card = state.card(id);
            assert(card != nullptr);
            if (ordinary_card_remains &&
                (card->kind() == CardKind::FourSuit ||
                 card->kind() == CardKind::ThreeSuitCat)) {
                continue;
            }
            actions.push_back(Action::play(
                id,
                PlayMode::Lead,
                card->intrinsic_suits().front()
            ));
        }
        return actions;
    }

    const std::optional<Suit> led_suit = trick.front().action.effective_suit();
    assert(led_suit.has_value());
    const bool has_led_suit = std::any_of(hand.begin(), hand.end(), [&](const CardId id) {
        const Card* card = state.card(id);
        assert(card != nullptr);
        return !is_multi_suit_card(*card) && std::find(
                   card->intrinsic_suits().begin(),
                   card->intrinsic_suits().end(),
                   *led_suit
               ) != card->intrinsic_suits().end();
    });

    for (const CardId id : hand) {
        const Card* card = state.card(id);
        assert(card != nullptr);
        if (card->kind() == CardKind::FourSuit && !has_led_suit) {
            actions.push_back(Action::play(id, PlayMode::Follow, *led_suit));
            actions.push_back(Action::play(id, PlayMode::Discard, Suit::NoTrump));
            if (const auto trump = trump_suit(state.contract());
                trump.has_value() && *trump != *led_suit) {
                actions.push_back(Action::play(id, PlayMode::Trump, *trump));
            }
            continue;
        }
        const bool matches_lead = std::find(
                                      card->intrinsic_suits().begin(),
                                      card->intrinsic_suits().end(),
                                      *led_suit
                                  ) != card->intrinsic_suits().end();
        if (has_led_suit && !matches_lead) {
            continue;
        }
        const Suit effective_suit = has_led_suit ? *led_suit : card->intrinsic_suits().front();
        actions.push_back(Action::play(
            id,
            has_led_suit ? PlayMode::Follow : PlayMode::Discard,
            effective_suit
        ));
    }
    return actions;
}

}  // namespace touniu_solver
