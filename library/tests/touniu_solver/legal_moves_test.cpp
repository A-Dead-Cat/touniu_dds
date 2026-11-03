#include <touniu_solver/legal_moves.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto normal_card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
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

}  // namespace
}  // namespace touniu_solver
