#include <touniu_solver/position_hash.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto normal(const std::uint64_t id, const Suit suit, const int rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

TEST(PositionHashTest, ApplyThenUndoRestoresExactHash)
{
    Scenario scenario;
    scenario.tricks_remaining = 1;
    scenario.hands[0] = {normal(1, Suit::Spades, 14)};
    auto position = *Position::create(scenario);
    const std::uint64_t before = hash_position(position);
    const Action action = position.legal_actions().front();

    ASSERT_TRUE(position.apply(action));
    EXPECT_NE(hash_position(position), before);
    position.undo();

    EXPECT_EQ(hash_position(position), before);
}

TEST(PositionHashTest, DirectionAndDrawIndexAffectHash)
{
    Scenario forward;
    forward.tricks_remaining = 1;
    forward.seed = 73U;
    forward.hands[0] = {Card::plus_two(CardId{1}, Suit::Spades)};
    Scenario reverse = forward;
    reverse.direction = Direction::Reverse;
    auto first = *Position::create(forward);
    const auto second = *Position::create(reverse);

    EXPECT_NE(hash_position(first), hash_position(second));
    ASSERT_TRUE(first.apply(first.legal_actions().front()));
    EXPECT_NE(first.draw_index(), 0U);
    EXPECT_NE(hash_position(first), hash_position(*Position::create(forward)));
}

}  // namespace
}  // namespace touniu_solver
