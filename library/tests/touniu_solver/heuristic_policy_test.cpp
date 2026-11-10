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

auto play_card(Position& position, const CardId id) -> bool
{
    for (const Action& action : position.legal_actions()) {
        if (!action.is_pass() && action.card_id() == id) {
            return position.apply(action);
        }
    }
    return false;
}

auto third_hand_spade_position(const bool partner_has_nine)
    -> std::optional<Position>
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(1, Suit::Spades, 3),
    };
    if (partner_has_nine) {
        scenario.hands[static_cast<std::size_t>(Seat::West)].push_back(
            card(2, Suit::Spades, 9));
    }
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        card(3, Suit::Hearts, 3),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(4, Suit::Spades, 10),
        card(5, Suit::Spades, 7),
        card(6, Suit::Spades, 4),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(7, Suit::Spades, 12),
        card(8, Suit::Spades, 8),
    };

    auto position = Position::create(scenario);
    if (!position.has_value() ||
        !play_card(*position, CardId{1}) ||
        !play_card(*position, CardId{3})) {
        return std::nullopt;
    }
    return position;
}

auto third_hand_club_position_with_intermediate_opponent()
    -> std::optional<Position>
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(20, Suit::Clubs, 10),
    };
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        card(21, Suit::Clubs, 2),
    };
    const auto intermediate = Rank::floating(10'571'428);
    if (!intermediate.has_value()) return std::nullopt;
    scenario.hands[static_cast<std::size_t>(Seat::North)].push_back(
        Card::floating(CardId{22}, Suit::Clubs, *intermediate));
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(23, Suit::Clubs, 11),
        card(24, Suit::Clubs, 8),
        card(25, Suit::Clubs, 5),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(26, Suit::Clubs, 14),
        card(27, Suit::Clubs, 7),
        card(28, Suit::Clubs, 4),
    };

    auto position = Position::create(scenario);
    if (!position.has_value() ||
        !play_card(*position, CardId{20}) ||
        !play_card(*position, CardId{21})) {
        return std::nullopt;
    }
    return position;
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

TEST(HeuristicPolicyTest, ThirdHandForcesTheQueensCoverToEstablishPartnersNine)
{
    auto position = third_hand_spade_position(true);
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{4});
}

TEST(HeuristicPolicyTest, ThirdHandForcesTheQueensCoverWithoutPartnersNine)
{
    auto position = third_hand_spade_position(false);
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{4});
}

TEST(HeuristicPolicyTest, ThirdHandKeepsJackForOpponentsIntermediateFloatingCard)
{
    auto position = third_hand_club_position_with_intermediate_opponent();
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{25});
}

TEST(HeuristicPolicyTest, FullInformationLeadUsesJackToDropSingletonFloatingTen)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 3;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(30, Suit::Spades, 11),
        card(31, Suit::Spades, 9),
        card(32, Suit::Spades, 5),
        card(33, Suit::Spades, 3),
    };
    const auto intermediate = Rank::floating(10'142'857);
    ASSERT_TRUE(intermediate.has_value());
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        Card::floating(CardId{34}, Suit::Spades, *intermediate),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(35, Suit::Spades, 10),
        card(36, Suit::Spades, 7),
        card(37, Suit::Spades, 4),
        card(38, Suit::Spades, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(39, Suit::Spades, 12),
        card(40, Suit::Spades, 8),
        card(41, Suit::Spades, 6),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{30});
}

TEST(HeuristicPolicyTest, FullInformationLeadUsesJackWithTenInTheSameHand)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 3;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(50, Suit::Spades, 11),
        card(51, Suit::Spades, 10),
        card(52, Suit::Spades, 9),
        card(53, Suit::Spades, 3),
    };
    const auto intermediate = Rank::floating(10'142'857);
    ASSERT_TRUE(intermediate.has_value());
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        Card::floating(CardId{54}, Suit::Spades, *intermediate),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(55, Suit::Spades, 7),
        card(56, Suit::Spades, 4),
        card(57, Suit::Spades, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(58, Suit::Spades, 12),
        card(59, Suit::Spades, 8),
        card(60, Suit::Spades, 6),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{50});
}

TEST(HeuristicPolicyTest, FloatingCardBreaksJackTenSequenceWhenItCannotBeDropped)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 3;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(70, Suit::Spades, 11),
        card(71, Suit::Spades, 10),
        card(72, Suit::Spades, 3),
    };
    const auto intermediate = Rank::floating(10'142'857);
    ASSERT_TRUE(intermediate.has_value());
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        Card::floating(CardId{73}, Suit::Spades, *intermediate),
        card(74, Suit::Spades, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(75, Suit::Spades, 7),
        card(76, Suit::Spades, 4),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(77, Suit::Spades, 12),
        card(78, Suit::Spades, 9),
        card(79, Suit::Spades, 8),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{72});
}

}  // namespace
}  // namespace touniu_solver
