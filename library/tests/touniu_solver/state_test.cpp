#include <touniu_solver/state.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto normal_card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

TEST(StateTest, EmptyCurrentPlayerRequiresTemporaryPassWithoutAddingCard)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<int>(Seat::North)] = {normal_card(1, Suit::Spades, 2)};

    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->requires_temporary_pass());
    EXPECT_TRUE(state->hand(Seat::West).empty());
    EXPECT_EQ(state->hand(Seat::North).size(), 1U);
    EXPECT_EQ(state->seed(), DefaultGenerationSeed);
}

TEST(StateTest, SameSeedAndDrawIndexProduceSameFloatingCard)
{
    Scenario scenario;
    scenario.seed = 73U;

    const auto first = State::create(scenario);
    const auto second = State::create(scenario);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    auto first_state = *first;
    auto second_state = *second;
    const Card first_draw = first_state.draw_floating(Seat::South);
    const Card second_draw = second_state.draw_floating(Seat::South);

    EXPECT_EQ(first_draw.kind(), CardKind::Floating);
    EXPECT_EQ(first_draw.rank(), second_draw.rank());
    EXPECT_EQ(first_draw.intrinsic_suits(), second_draw.intrinsic_suits());
    EXPECT_EQ(first_state.draw_index(), 1U);
    EXPECT_EQ(second_state.draw_index(), 1U);
}

TEST(StateTest, DrawsAdvanceDeterministicallyWithoutDuplicateEntity)
{
    Scenario scenario;
    scenario.seed = 73U;

    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    auto mutable_state = *state;
    const Card first_draw = mutable_state.draw_floating(Seat::East);
    const Card second_draw = mutable_state.draw_floating(Seat::East);

    EXPECT_NE(first_draw.id(), second_draw.id());
    EXPECT_EQ(mutable_state.hand(Seat::East).size(), 2U);
    EXPECT_EQ(mutable_state.draw_index(), 2U);
}

TEST(StateTest, TrickLimitEndsGameWithoutDiscardingStoredHands)
{
    Scenario scenario;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::South)] = {
        normal_card(7, Suit::Clubs, 5),
        normal_card(8, Suit::Clubs, 6),
    };

    const auto state = State::create(scenario);

    ASSERT_TRUE(state.has_value());
    auto mutable_state = *state;
    mutable_state.complete_trick();

    EXPECT_TRUE(mutable_state.is_finished());
    EXPECT_EQ(mutable_state.hand(Seat::South).size(), 2U);
}

}  // namespace
}  // namespace touniu_solver
