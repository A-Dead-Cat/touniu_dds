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

auto is_joker(const Card& card) -> bool
{
    return card.kind() == CardKind::BigJoker || card.kind() == CardKind::SmallJoker;
}

auto joker_trump_suit(const Contract contract) -> Suit
{
    const auto suit = trump_suit(contract);
    return suit.value_or(Suit::NoTrump);
}

auto is_real_card_of_suit(const State& state, const Card& card, const Suit suit) -> bool
{
    if (is_joker(card)) {
        return suit == joker_trump_suit(state.contract());
    }
    return std::find(card.intrinsic_suits().begin(), card.intrinsic_suits().end(), suit) !=
           card.intrinsic_suits().end();
}

auto is_trump_card(const State& state, const Card& card) -> bool
{
    if (is_joker(card)) {
        return true;
    }
    const Suit trump = joker_trump_suit(state.contract());
    return std::find(card.intrinsic_suits().begin(), card.intrinsic_suits().end(), trump) !=
           card.intrinsic_suits().end();
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
            if (card->kind() == CardKind::ThreeSuitCat) {
                actions.push_back(Action::play(id, PlayMode::Lead, Suit::NoTrump));
                continue;
            }
            if (is_joker(*card)) {
                actions.push_back(Action::play(
                    id, PlayMode::Lead, joker_trump_suit(state.contract())));
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
    const Card* lead_card = trick.front().action.is_pass()
        ? nullptr : state.card(*trick.front().action.card_id());
    const bool has_led_suit = std::any_of(hand.begin(), hand.end(), [&](const CardId id) {
        const Card* card = state.card(id);
        assert(card != nullptr);
        return !is_multi_suit_card(*card) && is_real_card_of_suit(state, *card, *led_suit);
    });

    if (state.contract() == Contract::NoTrump && *led_suit != Suit::NoTrump &&
        !has_led_suit) {
        const auto cat = std::find_if(hand.begin(), hand.end(), [&](const CardId id) {
            return state.card(id)->kind() == CardKind::ThreeSuitCat;
        });
        if (cat != hand.end()) {
            return {Action::play(*cat, PlayMode::Follow, *led_suit)};
        }
    }

    for (const CardId id : hand) {
        const Card* card = state.card(id);
        assert(card != nullptr);
        if (card->kind() == CardKind::FourSuit &&
            !has_led_suit && state.contract() == Contract::NoTrump &&
            lead_card != nullptr &&
            (lead_card->kind() == CardKind::BigJoker ||
             lead_card->kind() == CardKind::SmallJoker ||
             lead_card->kind() == CardKind::NoTrump)) {
            actions.push_back(Action::play(id, PlayMode::Discard, Suit::NoTrump));
            continue;
        }
        if (card->kind() == CardKind::ThreeSuitCat) {
            if (state.contract() == Contract::NoTrump) {
                if (*led_suit == Suit::NoTrump && !has_led_suit) {
                    actions.push_back(Action::play(id, PlayMode::Discard, Suit::NoTrump));
                }
                continue;
            }
            const auto trump = trump_suit(state.contract());
            if (*led_suit == Suit::NoTrump || (trump.has_value() && *led_suit == *trump)) {
                if (!has_led_suit) {
                    actions.push_back(Action::play(id, PlayMode::Discard, Suit::NoTrump));
                }
            } else {
                actions.push_back(Action::play(id, PlayMode::Follow, *led_suit));
                if (!has_led_suit) {
                    actions.push_back(Action::play(id, PlayMode::Discard, Suit::NoTrump));
                }
            }
            continue;
        }
        if (card->kind() == CardKind::FourSuit && !has_led_suit) {
            actions.push_back(Action::play(id, PlayMode::Follow, *led_suit));
            actions.push_back(Action::play(id, PlayMode::Discard, Suit::NoTrump));
            if (const auto trump = trump_suit(state.contract());
                trump.has_value() && *trump != *led_suit) {
                actions.push_back(Action::play(id, PlayMode::Trump, *trump));
            }
            continue;
        }
        const bool matches_lead = is_real_card_of_suit(state, *card, *led_suit);
        if (has_led_suit && !matches_lead) {
            continue;
        }
        const Suit effective_suit = has_led_suit
            ? *led_suit
            : (is_joker(*card)
                   ? joker_trump_suit(state.contract())
                   : card->intrinsic_suits().front());
        actions.push_back(Action::play(
            id,
            has_led_suit ? PlayMode::Follow
                         : (is_trump_card(state, *card)
                                ? PlayMode::Trump : PlayMode::Discard),
            effective_suit
        ));
    }
    return actions;
}

}  // namespace touniu_solver
