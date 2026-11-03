#include <touniu_solver/position.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto normal_card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

auto plus_two_card(const std::uint64_t id, const Suit suit) -> Card
{
    return Card::plus_two(CardId{id}, suit);
}

auto reverse_card(const std::uint64_t id, const Suit suit) -> Card
{
    return Card::reverse(CardId{id}, suit);
}

auto block_card(const std::uint64_t id, const Suit suit) -> Card
{
    return Card::block(CardId{id}, suit);
}

auto action_for(const std::vector<Action>& actions, const CardId id, const PlayMode mode)
    -> Action
{
    for (const Action& action : actions) {
        if (action.card_id() == id && action.mode() == mode) {
            return action;
        }
    }
    return Action::pass();
}

TEST(PositionTest, ApplyPlayRemovesCardAndAdvancesPlayer)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {
        normal_card(1, Suit::Spades, 14),
        normal_card(2, Suit::Hearts, 3),
    };
    auto position = Position::create(scenario);

    ASSERT_TRUE(position.has_value());
    const auto actions = position->legal_actions();
    const Action lead = action_for(actions, CardId{1}, PlayMode::Lead);
    ASSERT_FALSE(lead.is_pass());

    ASSERT_TRUE(position->apply(lead));
    EXPECT_EQ(position->hand(Seat::West).size(), 1U);
    EXPECT_EQ(position->next_player(), Seat::North);

    position->undo();
    EXPECT_EQ(position->hand(Seat::West).size(), 2U);
    EXPECT_EQ(position->next_player(), Seat::West);
}

TEST(PositionTest, FullTrickCollectsAndUndoRestores)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {normal_card(2, Suit::Spades, 13)};
    scenario.hands[static_cast<int>(Seat::East)] = {normal_card(3, Suit::Spades, 12)};
    scenario.hands[static_cast<int>(Seat::South)] = {normal_card(4, Suit::Spades, 11)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const auto play = [&](const CardId id) {
        const auto actions = position->legal_actions();
        return position->apply(action_for(actions, id, PlayMode::Lead)) ||
               position->apply(action_for(actions, id, PlayMode::Follow));
    };

    ASSERT_TRUE(play(CardId{1}));
    ASSERT_TRUE(play(CardId{2}));
    ASSERT_TRUE(play(CardId{3}));
    ASSERT_TRUE(play(CardId{4}));

    EXPECT_TRUE(position->is_finished());
    EXPECT_EQ(position->tricks_remaining(), 0U);
    EXPECT_EQ(position->tricks_won(Seat::West), 1U);
    EXPECT_EQ(position->next_player(), Seat::West);
    EXPECT_TRUE(position->current_trick().empty());

    position->undo();
    EXPECT_FALSE(position->is_finished());
    EXPECT_EQ(position->tricks_remaining(), 1U);
    EXPECT_EQ(position->tricks_won(Seat::West), 0U);
    EXPECT_EQ(position->next_player(), Seat::South);
    EXPECT_EQ(position->current_trick().size(), 3U);
    EXPECT_EQ(position->hand(Seat::South).size(), 1U);
}

TEST(PositionTest, PlusTwoDrawsAndUndoRestoresDrawIndex)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {plus_two_card(1, Suit::Spades)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const auto actions = position->legal_actions();
    const Action lead = action_for(actions, CardId{1}, PlayMode::Lead);
    ASSERT_FALSE(lead.is_pass());
    ASSERT_TRUE(position->apply(lead));

    EXPECT_EQ(position->hand(Seat::North).size(), 2U);
    EXPECT_EQ(position->draw_index(), 2U);
    EXPECT_EQ(position->next_player(), Seat::North);

    position->undo();
    EXPECT_EQ(position->hand(Seat::North).size(), 0U);
    EXPECT_EQ(position->draw_index(), 0U);
    EXPECT_EQ(position->next_player(), Seat::West);
    EXPECT_EQ(position->hand(Seat::West).size(), 1U);
}

TEST(PositionTest, ReverseFlipsDirectionAtCollectionAndUndoRestores)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {normal_card(2, Suit::Spades, 13)};
    scenario.hands[static_cast<int>(Seat::East)] = {normal_card(3, Suit::Spades, 12)};
    scenario.hands[static_cast<int>(Seat::South)] = {reverse_card(4, Suit::Spades)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const auto play = [&](const CardId id) {
        const auto actions = position->legal_actions();
        const Action lead = action_for(actions, id, PlayMode::Lead);
        const Action follow = action_for(actions, id, PlayMode::Follow);
        return position->apply(!lead.is_pass() ? lead : follow);
    };

    ASSERT_TRUE(play(CardId{1}));
    ASSERT_TRUE(play(CardId{2}));
    ASSERT_TRUE(play(CardId{3}));
    ASSERT_TRUE(play(CardId{4}));

    EXPECT_EQ(position->direction(), Direction::Reverse);

    position->undo();
    EXPECT_EQ(position->direction(), Direction::Forward);
    EXPECT_EQ(position->valid_reverse_count(), 0U);
}

TEST(PositionTest, BlockForcesPassAndUndoRestores)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {block_card(2, Suit::Spades)};
    scenario.hands[static_cast<int>(Seat::East)] = {};
    scenario.hands[static_cast<int>(Seat::South)] = {};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    ASSERT_TRUE(position->apply(action_for(position->legal_actions(), CardId{1}, PlayMode::Lead)));
    ASSERT_TRUE(position->apply(action_for(position->legal_actions(), CardId{2}, PlayMode::Follow)));

    const auto forced = position->legal_actions();
    ASSERT_EQ(forced.size(), 1U);
    EXPECT_TRUE(forced.front().is_pass());

    position->undo();
    const auto after_undo = position->legal_actions();
    EXPECT_FALSE(after_undo.size() == 1U && after_undo.front().is_pass());
    EXPECT_EQ(position->hand(Seat::North).size(), 1U);
}

}  // namespace
}  // namespace touniu_solver
