#include <touniu_solver/position.hpp>

#include "library/tools/heuristic_policy.hpp"

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

void add_suit(
    Scenario& scenario,
    const Seat seat,
    const Suit suit,
    const std::initializer_list<std::int32_t> ranks,
    std::uint64_t& next_id)
{
    auto& hand = scenario.hands[static_cast<std::size_t>(seat)];
    for (const std::int32_t rank : ranks) {
        hand.push_back(card(next_id++, suit, rank));
    }
}

TEST(HeuristicPolicyTest, FullInformationLeadAvoidsDeclarersLongSuitWhenDummyIsVoid)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 11;
    std::uint64_t id = 1;

    add_suit(scenario, Seat::West, Suit::Spades, {9, 5, 3}, id);
    add_suit(scenario, Seat::West, Suit::Hearts, {8, 7, 5}, id);
    add_suit(scenario, Seat::West, Suit::Diamonds, {12, 8, 7}, id);
    add_suit(scenario, Seat::West, Suit::Clubs, {9, 6}, id);

    add_suit(scenario, Seat::North, Suit::Hearts, {11, 10, 9, 6, 3}, id);
    auto floating = Rank::floating(10'285'714);
    ASSERT_TRUE(floating.has_value());
    scenario.hands[static_cast<std::size_t>(Seat::North)].push_back(
        Card::floating(CardId{id++}, Suit::Hearts, *floating));
    add_suit(scenario, Seat::North, Suit::Diamonds, {6}, id);
    add_suit(scenario, Seat::North, Suit::Clubs, {12, 3}, id);

    add_suit(scenario, Seat::East, Suit::Spades, {10, 7, 4}, id);
    add_suit(scenario, Seat::East, Suit::Hearts, {12, 4}, id);
    add_suit(scenario, Seat::East, Suit::Diamonds, {10, 9, 5, 2}, id);
    add_suit(scenario, Seat::East, Suit::Clubs, {8, 5}, id);

    add_suit(scenario, Seat::South, Suit::Spades, {12, 8}, id);
    add_suit(scenario, Seat::South, Suit::Diamonds, {14, 13, 11, 4, 3}, id);
    add_suit(scenario, Seat::South, Suit::Clubs, {14, 4}, id);

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    const Action chosen = heuristic_choose(*position, 3);
    ASSERT_TRUE(chosen.card_id().has_value());
    const Card* chosen_card = position->card(*chosen.card_id());
    ASSERT_NE(chosen_card, nullptr);
    ASSERT_EQ(chosen_card->intrinsic_suits().size(), 1U);
    EXPECT_NE(chosen_card->intrinsic_suits().front(), Suit::Hearts);
}

}  // namespace
}  // namespace touniu_solver
