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

TEST(HeuristicPolicyTest, ConsecutiveDiscardsPreserveEstablishedPartnershipSpades)
{
    Scenario scenario;
    scenario.next_player = Seat::South;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 9;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(100, Suit::Spades, 13),
        card(101, Suit::Spades, 9),
        card(102, Suit::Spades, 8),
        card(103, Suit::Hearts, 9),
        card(104, Suit::Hearts, 8),
        card(105, Suit::Hearts, 6),
        card(106, Suit::Diamonds, 12),
        card(107, Suit::Diamonds, 11),
        card(108, Suit::Diamonds, 10),
    };
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        card(110, Suit::Hearts, 13),
        card(111, Suit::Hearts, 5),
        card(112, Suit::Hearts, 4),
        card(113, Suit::Clubs, 5),
        card(114, Suit::Clubs, 4),
        card(115, Suit::Diamonds, 14),
        card(116, Suit::Diamonds, 6),
        card(117, Suit::Diamonds, 5),
        card(118, Suit::Diamonds, 4),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(120, Suit::Spades, 5),
        card(121, Suit::Hearts, 12),
        card(122, Suit::Hearts, 11),
        card(123, Suit::Hearts, 10),
        card(124, Suit::Clubs, 9),
        card(125, Suit::Clubs, 8),
        card(126, Suit::Diamonds, 9),
        card(127, Suit::Diamonds, 8),
        card(128, Suit::Diamonds, 3),
    };
    const auto south_spade = Rank::floating(12'999'999);
    ASSERT_TRUE(south_spade.has_value());
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        Card::floating(CardId{130}, Suit::Spades, *south_spade),
        card(131, Suit::Hearts, 14),
        card(132, Suit::Hearts, 7),
        card(133, Suit::Hearts, 2),
        card(134, Suit::Clubs, 14),
        card(135, Suit::Clubs, 12),
        card(136, Suit::Diamonds, 13),
        card(137, Suit::Diamonds, 7),
        card(138, Suit::Diamonds, 2),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    ASSERT_TRUE(play_card(*position, CardId{135}));

    const Action first = heuristic_choose(*position, 3);
    EXPECT_EQ(first.card_id(), CardId{105});
    ASSERT_TRUE(position->apply(first));
    ASSERT_TRUE(play_card(*position, CardId{113}));
    ASSERT_TRUE(play_card(*position, CardId{125}));
    ASSERT_TRUE(play_card(*position, CardId{134}));

    const Action second = heuristic_choose(*position, 3);
    EXPECT_EQ(second.card_id(), CardId{104});
}

TEST(HeuristicPolicyTest, DoesNotTrumpAnOpponentsWinningCherryBomb)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(200, Suit::Clubs, 5), card(201, Suit::Clubs, 4),
    };
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        Card::cherry_bomb(CardId{202}, Suit::Hearts),
        card(203, Suit::Hearts, 3),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        Card::big_joker(CardId{204}),
        card(205, Suit::Clubs, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(206, Suit::Diamonds, 5), card(207, Suit::Diamonds, 4),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    ASSERT_TRUE(play_card(*position, CardId{202}));

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{205});
}

TEST(HeuristicPolicyTest, DiscardsAnUnreachableTopInsteadOfAWorkingStopper)
{
    Scenario scenario;
    scenario.next_player = Seat::South;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(220, Suit::Hearts, 14), card(221, Suit::Diamonds, 12),
    };
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        card(222, Suit::Spades, 9), card(223, Suit::Diamonds, 3),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(224, Suit::Spades, 7), card(225, Suit::Clubs, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(226, Suit::Spades, 14), card(227, Suit::Diamonds, 11),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    ASSERT_TRUE(play_card(*position, CardId{226}));

    const Action chosen = heuristic_choose(*position, 3);

    EXPECT_EQ(chosen.card_id(), CardId{220});
}

TEST(HeuristicPolicyTest, EastDoesNotOpenTheHalfFrozenClubSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::East;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 11;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(240, Suit::Spades, 11), card(241, Suit::Spades, 9),
        card(242, Suit::Spades, 5), card(243, Suit::Spades, 3),
        card(244, Suit::Hearts, 8), card(245, Suit::Hearts, 7),
        card(246, Suit::Clubs, 9), card(247, Suit::Clubs, 6),
        card(248, Suit::Diamonds, 12), card(249, Suit::Diamonds, 8),
        card(250, Suit::Diamonds, 7),
    };
    const auto north_spade = Rank::floating(10'142'857);
    const auto north_club = Rank::floating(10'571'428);
    ASSERT_TRUE(north_spade.has_value());
    ASSERT_TRUE(north_club.has_value());
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        Card::floating(CardId{251}, Suit::Spades, *north_spade),
        card(252, Suit::Hearts, 14), card(253, Suit::Hearts, 11),
        card(254, Suit::Hearts, 10), card(255, Suit::Hearts, 9),
        card(256, Suit::Hearts, 6), card(257, Suit::Hearts, 3),
        card(258, Suit::Clubs, 12),
        Card::floating(CardId{259}, Suit::Clubs, *north_club),
        card(260, Suit::Clubs, 3), card(261, Suit::Diamonds, 6),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(262, Suit::Spades, 10), card(263, Suit::Spades, 7),
        card(264, Suit::Spades, 4), card(265, Suit::Spades, 2),
        card(266, Suit::Hearts, 4), card(267, Suit::Clubs, 11),
        card(268, Suit::Clubs, 8), card(269, Suit::Diamonds, 10),
        card(270, Suit::Diamonds, 9), card(271, Suit::Diamonds, 5),
        card(272, Suit::Diamonds, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(273, Suit::Spades, 14), card(274, Suit::Spades, 12),
        card(275, Suit::Spades, 8), card(276, Suit::Spades, 6),
        card(277, Suit::Clubs, 7), card(278, Suit::Clubs, 4),
        card(279, Suit::Diamonds, 14), card(280, Suit::Diamonds, 13),
        card(281, Suit::Diamonds, 11), card(282, Suit::Diamonds, 4),
        card(283, Suit::Diamonds, 3),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const Action chosen = heuristic_choose(*position, 3);
    ASSERT_TRUE(chosen.card_id().has_value());
    const Card* chosen_card = position->card(*chosen.card_id());
    ASSERT_NE(chosen_card, nullptr);
    ASSERT_EQ(chosen_card->intrinsic_suits().size(), 1U);
    EXPECT_NE(chosen_card->intrinsic_suits().front(), Suit::Clubs);

    scenario.next_player = Seat::West;
    auto west_position = Position::create(scenario);
    ASSERT_TRUE(west_position.has_value());
    const Action west_choice = heuristic_choose(*west_position, 3);
    ASSERT_TRUE(west_choice.card_id().has_value());
    const Card* west_card = west_position->card(*west_choice.card_id());
    ASSERT_NE(west_card, nullptr);
    ASSERT_EQ(west_card->intrinsic_suits().size(), 1U);
    EXPECT_EQ(west_card->intrinsic_suits().front(), Suit::Clubs);
}

TEST(HeuristicPolicyTest, HalfFrozenLeadMayEstablishLongSuitWithoutPartnerEntry)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 4;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(300, Suit::Spades, 9), card(301, Suit::Spades, 4),
        card(302, Suit::Hearts, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        card(303, Suit::Spades, 8), card(304, Suit::Spades, 7),
        card(305, Suit::Spades, 6), card(306, Suit::Hearts, 14),
        card(307, Suit::Hearts, 12), card(308, Suit::Hearts, 10),
        card(309, Suit::Hearts, 8),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(310, Suit::Spades, 10), card(311, Suit::Spades, 5),
        card(312, Suit::Spades, 2), card(313, Suit::Hearts, 3),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(314, Suit::Spades, 11), card(315, Suit::Spades, 3),
        card(316, Suit::Hearts, 13), card(317, Suit::Hearts, 11),
        card(318, Suit::Hearts, 9), card(319, Suit::Hearts, 6),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    const Action chosen = heuristic_choose(*position, 3);
    ASSERT_TRUE(chosen.card_id().has_value());
    const Card* chosen_card = position->card(*chosen.card_id());
    ASSERT_NE(chosen_card, nullptr);
    ASSERT_EQ(chosen_card->intrinsic_suits().size(), 1U);
    EXPECT_EQ(chosen_card->intrinsic_suits().front(), Suit::Spades);
}

TEST(HeuristicPolicyTest, FrozenLeadMayEstablishLongSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 4;
    scenario.hands[static_cast<std::size_t>(Seat::West)] = {
        card(340, Suit::Spades, 9), card(341, Suit::Spades, 7),
        card(342, Suit::Spades, 3), card(343, Suit::Hearts, 2),
    };
    scenario.hands[static_cast<std::size_t>(Seat::North)] = {
        card(344, Suit::Spades, 8), card(345, Suit::Spades, 5),
        card(346, Suit::Spades, 4), card(347, Suit::Hearts, 14),
        card(354, Suit::Hearts, 12), card(355, Suit::Hearts, 10),
        card(356, Suit::Hearts, 8),
    };
    scenario.hands[static_cast<std::size_t>(Seat::East)] = {
        card(348, Suit::Spades, 10), card(349, Suit::Spades, 2),
        card(350, Suit::Hearts, 3),
    };
    scenario.hands[static_cast<std::size_t>(Seat::South)] = {
        card(351, Suit::Spades, 11), card(352, Suit::Spades, 6),
        card(353, Suit::Hearts, 13), card(357, Suit::Hearts, 11),
        card(358, Suit::Hearts, 9), card(359, Suit::Hearts, 6),
    };

    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    const Action chosen = heuristic_choose(*position, 3);
    ASSERT_TRUE(chosen.card_id().has_value());
    const Card* chosen_card = position->card(*chosen.card_id());
    ASSERT_NE(chosen_card, nullptr);
    ASSERT_EQ(chosen_card->intrinsic_suits().size(), 1U);
    EXPECT_EQ(chosen_card->intrinsic_suits().front(), Suit::Spades);
}

}  // namespace
}  // namespace touniu_solver
