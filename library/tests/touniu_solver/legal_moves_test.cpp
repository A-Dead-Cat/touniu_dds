#include <touniu_solver/legal_moves.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto normal_card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

TEST(LegalMovesTest, NoTrumpSuitSpecialIsDiscardedRatherThanRuffed)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::special(CardId{1}, CardKind::PlusTwo, {Suit::NoTrump}),
    };
    auto state = State::create(scenario);
    ASSERT_TRUE(state.has_value());
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::Spades)}};

    const std::vector<Action> actions = legal_actions(*state, trick);

    ASSERT_EQ(actions.size(), 1U);
    // The rules engine validates a special's effect only when it is led or
    // follows the lead family, so an off-suit +2NT is played as a discard.
    EXPECT_EQ(
        actions.front(),
        Action::play(CardId{1}, PlayMode::Discard, Suit::NoTrump));
}

TEST(LegalMovesTest, NoTrumpSuitSpecialFollowsANoTrumpLead)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::special(CardId{1}, CardKind::PlusTwo, {Suit::NoTrump}),
    };
    auto state = State::create(scenario);
    ASSERT_TRUE(state.has_value());
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::NoTrump)}};

    const std::vector<Action> actions = legal_actions(*state, trick);

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(
        actions.front(),
        Action::play(CardId{1}, PlayMode::Follow, Suit::NoTrump));
}

TEST(LegalMovesTest, RequiresNormalSuitWhenPlayerHasLedSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        normal_card(1, Suit::Spades, 2),
        normal_card(2, Suit::Hearts, 3),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Follow, Suit::Spades));
}

TEST(LegalMovesTest, AllowsDiscardWhenPlayerHasNoLedSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        normal_card(1, Suit::Hearts, 2),
        normal_card(2, Suit::Diamonds, 3),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    EXPECT_EQ(actions.size(), 2U);
    EXPECT_EQ(actions[0], Action::play(CardId{1}, PlayMode::Discard, Suit::Hearts));
    EXPECT_EQ(actions[1], Action::play(CardId{2}, PlayMode::Discard, Suit::Diamonds));
}

TEST(LegalMovesTest, ProducesOnlyTemporaryPassForEmptyHand)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, {});

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_TRUE(actions.front().is_pass());
}

TEST(LegalMovesTest, FourSuitCardCannotLeadWhileAnOrdinaryCardRemains)
{
    Scenario scenario;
    scenario.next_player = Seat::South;
    scenario.hands[static_cast<int>(Seat::South)] = {
        Card::four_suit(CardId{1}, Rank::integer(12)),
        normal_card(2, Suit::Clubs, 3),
    };
    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, {});

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{2}, PlayMode::Lead, Suit::Clubs));
}

TEST(LegalMovesTest, ThreeSuitCatCannotLeadWhileAnOrdinaryCardRemains)
{
    Scenario scenario;
    scenario.next_player = Seat::South;
    scenario.hands[static_cast<int>(Seat::South)] = {
        Card::three_suit_cat(CardId{1}),
        normal_card(2, Suit::Hearts, 3),
    };
    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, {});

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{2}, PlayMode::Lead, Suit::Hearts));
}

TEST(LegalMovesTest, ThreeSuitCatMayFollowAnOrdinaryLeadWhileHoldingThatSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::South;
    scenario.contract = Contract::NoTrump;
    scenario.hands[static_cast<int>(Seat::South)] = {
        Card::three_suit_cat(CardId{1}),
        normal_card(2, Suit::Spades, 12),
        normal_card(3, Suit::Spades, 6),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::East, Action::play(CardId{9}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    // Both spades and the cat: the cat follows the lead as the lowest card of
    // the trick, so its holder can duck a round without spending a spade.
    ASSERT_EQ(actions.size(), 3U);
    EXPECT_NE(
        std::find(
            actions.begin(),
            actions.end(),
            Action::play(CardId{1}, PlayMode::Follow, Suit::Spades)
        ),
        actions.end()
    );
}

TEST(LegalMovesTest, ThreeSuitCatMustFollowWhenVoidInTheLedSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::South;
    scenario.contract = Contract::NoTrump;
    scenario.hands[static_cast<int>(Seat::South)] = {
        Card::three_suit_cat(CardId{1}),
        normal_card(2, Suit::Hearts, 3),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::East, Action::play(CardId{9}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(
        actions.front(),
        Action::play(CardId{1}, PlayMode::Follow, Suit::Spades));
}

TEST(LegalMovesTest, FourSuitCardMayFollowOrDiscardWhenVoidInTheLedSuit)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::four_suit(CardId{1}, Rank::integer(12)),
        normal_card(2, Suit::Hearts, 3),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    EXPECT_EQ(actions.size(), 3U);
    EXPECT_NE(
        std::find(
            actions.begin(),
            actions.end(),
            Action::play(CardId{1}, PlayMode::Follow, Suit::Spades)
        ),
        actions.end()
    );
    EXPECT_NE(
        std::find(
            actions.begin(),
            actions.end(),
            Action::play(CardId{1}, PlayMode::Discard, Suit::NoTrump)
        ),
        actions.end()
    );
}

TEST(LegalMovesTest, FourSuitCardMayTrumpWhenVoidInTheLedSuitUnderSuitContract)
{
    Scenario scenario;
    scenario.contract = Contract::Hearts;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::four_suit(CardId{1}, Rank::integer(12)),
        normal_card(2, Suit::Clubs, 3),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    EXPECT_NE(
        std::find(
            actions.begin(),
            actions.end(),
            Action::play(CardId{1}, PlayMode::Trump, Suit::Hearts)
        ),
        actions.end()
    );
}

TEST(LegalMovesTest, FourSuitCardDoesNotCreateASeparateTrumpChoiceWhenTrumpLeads)
{
    Scenario scenario;
    scenario.contract = Contract::Hearts;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::four_suit(CardId{1}, Rank::integer(12)),
        normal_card(2, Suit::Clubs, 3),
    };
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Hearts)}};

    ASSERT_TRUE(state.has_value());
    const std::vector<Action> actions = legal_actions(*state, trick);

    EXPECT_EQ(actions.size(), 3U);
    EXPECT_EQ(
        std::count(
            actions.begin(),
            actions.end(),
            Action::play(CardId{1}, PlayMode::Follow, Suit::Hearts)
        ),
        1
    );
    EXPECT_EQ(
        std::count(
            actions.begin(),
            actions.end(),
            Action::play(CardId{1}, PlayMode::Trump, Suit::Hearts)
        ),
        0
    );
}

TEST(LegalMovesTest, SuitContractCatMayFollowOrDiscardWhenVoidInNonTrumpLead)
{
    Scenario scenario;
    scenario.contract = Contract::Hearts;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::three_suit_cat(CardId{1}), normal_card(2, Suit::Clubs, 3)};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    EXPECT_NE(std::find(actions.begin(), actions.end(),
        Action::play(CardId{1}, PlayMode::Follow, Suit::Spades)), actions.end());
    EXPECT_NE(std::find(actions.begin(), actions.end(),
        Action::play(CardId{1}, PlayMode::Discard, Suit::NoTrump)), actions.end());
}

TEST(LegalMovesTest, NoTrumpCatMustFollowBeforeOtherDiscardsWhenVoid)
{
    Scenario scenario;
    scenario.contract = Contract::NoTrump;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::three_suit_cat(CardId{1}), normal_card(2, Suit::Clubs, 3)};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{3}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Follow, Suit::Spades));
}

TEST(LegalMovesTest, FourSuitCanOnlyDiscardWhenJokerLeadsInNoTrump)
{
    Scenario scenario;
    scenario.contract = Contract::NoTrump;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::West)] = {Card::big_joker(CardId{9})};
    scenario.hands[static_cast<int>(Seat::North)] = {Card::four_suit(CardId{1}, Rank::integer(12))};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::NoTrump)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Discard, Suit::NoTrump));
}

TEST(LegalMovesTest, NoTrumpCatMayDiscardWhenJokerLeads)
{
    Scenario scenario;
    scenario.contract = Contract::NoTrump;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::West)] = {Card::big_joker(CardId{9})};
    scenario.hands[static_cast<int>(Seat::North)] = {Card::three_suit_cat(CardId{1})};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::NoTrump)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Discard, Suit::NoTrump));
}

TEST(LegalMovesTest, JokerMustFollowJokerLeadBeforeCatCanDiscardInNoTrump)
{
    Scenario scenario;
    scenario.contract = Contract::NoTrump;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::West)] = {Card::big_joker(CardId{9})};
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::small_joker(CardId{2}), Card::three_suit_cat(CardId{1})};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::NoTrump)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{2}, PlayMode::Follow, Suit::NoTrump));
}

TEST(LegalMovesTest, NoTrumpCardTrumpsWhenVoidInOrdinaryLead)
{
    Scenario scenario;
    scenario.contract = Contract::NoTrump;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::no_trump(CardId{1}, Rank::integer(9))};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Trump, Suit::NoTrump));
}

TEST(LegalMovesTest, SuitTrumpTrumpsWhenVoidInLedSuit)
{
    Scenario scenario;
    scenario.contract = Contract::Hearts;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        normal_card(1, Suit::Hearts, 4)};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Trump, Suit::Hearts));
}

TEST(LegalMovesTest, RealNoTrumpMustFollowBeforeFourSuitCanDiscard)
{
    Scenario scenario;
    scenario.contract = Contract::NoTrump;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::West)] = {Card::big_joker(CardId{9})};
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::no_trump(CardId{1}, Rank::integer(8)),
        Card::four_suit(CardId{2}, Rank::integer(12))};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::NoTrump)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{1}, PlayMode::Follow, Suit::NoTrump));
}

TEST(LegalMovesTest, FourSuitSpecialActsAsWildCardWhenVoid)
{
    Scenario scenario;
    scenario.contract = Contract::Hearts;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(9, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::special(
            CardId{1},
            CardKind::Block,
            {Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs})};
    const auto state = State::create(scenario);
    const Trick trick{{Seat::West, Action::play(CardId{9}, PlayMode::Lead, Suit::Spades)}};

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, trick);

    ASSERT_EQ(actions.size(), 3U);
    EXPECT_EQ(actions[0], Action::play(CardId{1}, PlayMode::Follow, Suit::Spades));
    EXPECT_EQ(actions[1], Action::play(CardId{1}, PlayMode::Discard, Suit::NoTrump));
    EXPECT_EQ(actions[2], Action::play(CardId{1}, PlayMode::Trump, Suit::Hearts));
}

TEST(LegalMovesTest, FourSuitSpecialCannotLeadWhileOrdinaryCardRemains)
{
    Scenario scenario;
    scenario.next_player = Seat::North;
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::special(
            CardId{1},
            CardKind::Reverse,
            {Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs}),
        normal_card(2, Suit::Clubs, 3)};
    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    const auto actions = legal_actions(*state, {});

    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front(), Action::play(CardId{2}, PlayMode::Lead, Suit::Clubs));
}

}  // namespace
}  // namespace touniu_solver
