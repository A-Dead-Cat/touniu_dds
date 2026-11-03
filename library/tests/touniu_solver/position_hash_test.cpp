#include <touniu_solver/live_position.hpp>
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

TEST(PositionIdentityTest, HandOrderDoesNotChangeIdentity)
{
    Scenario first;
    first.tricks_remaining = 1;
    first.hands[0] = {normal(1, Suit::Spades, 14), normal(2, Suit::Spades, 2)};
    Scenario second;
    second.tricks_remaining = 1;
    second.hands[0] = {normal(2, Suit::Spades, 2), normal(1, Suit::Spades, 14)};

    const PositionIdentity first_identity =
        recompute_position_identity(*Position::create(first));
    const PositionIdentity second_identity =
        recompute_position_identity(*Position::create(second));

    EXPECT_EQ(first_identity, second_identity);
}

TEST(PositionIdentityTest, SemanticFieldsChangeIdentity)
{
    Scenario base;
    base.tricks_remaining = 1;
    base.seed = 7U;
    base.hands[0] = {normal(1, Suit::Spades, 14), normal(2, Suit::Spades, 2)};
    base.hands[1] = {normal(3, Suit::Spades, 13)};
    const PositionIdentity identity = recompute_position_identity(*Position::create(base));

    Scenario changed = base;
    changed.contract = Contract::Hearts;
    EXPECT_NE(recompute_position_identity(*Position::create(changed)), identity);

    changed = base;
    changed.seed = 8U;
    EXPECT_NE(recompute_position_identity(*Position::create(changed)), identity);

    changed = base;
    changed.tricks_remaining = 2;
    EXPECT_NE(recompute_position_identity(*Position::create(changed)), identity);

    changed = base;
    changed.next_player = Seat::North;
    EXPECT_NE(recompute_position_identity(*Position::create(changed)), identity);

    changed = base;
    changed.direction = Direction::Reverse;
    EXPECT_NE(recompute_position_identity(*Position::create(changed)), identity);
}

TEST(PositionIdentityTest, GeneratedCardsWithSameRankButDifferentIdsDiffer)
{
    const Rank rank = *Rank::floating(1'500'001);
    Scenario first;
    first.tricks_remaining = 1;
    first.hands[0] = {Card::floating(CardId{1}, Suit::Spades, rank)};
    Scenario second;
    second.tricks_remaining = 1;
    second.hands[0] = {Card::floating(CardId{2}, Suit::Spades, rank)};

    EXPECT_NE(
        recompute_position_identity(*Position::create(first)),
        recompute_position_identity(*Position::create(second)));
}

TEST(PositionIdentityTest, RetiredCatalogCardsAffectIdentity)
{
    Scenario history;
    history.next_player = Seat::West;
    history.tricks_remaining = 2;
    history.contract = Contract::NoTrump;
    history.hands[0] = {normal(1, Suit::Spades, 14), normal(10, Suit::Spades, 2)};
    history.hands[1] = {normal(2, Suit::Spades, 13), normal(11, Suit::Spades, 3)};
    history.hands[2] = {normal(3, Suit::Spades, 12), normal(12, Suit::Spades, 4)};
    history.hands[3] = {normal(4, Suit::Spades, 11), normal(13, Suit::Spades, 5)};
    auto played = *Position::create(history);
    for (int index = 0; index < 4; ++index) {
        ASSERT_TRUE(played.apply(played.legal_actions().front()));
    }
    ASSERT_EQ(played.tricks_remaining(), 1U);
    ASSERT_EQ(played.tricks_won(Seat::West), 1U);
    ASSERT_EQ(played.card_catalog().size(), 8U);

    LivePosition retired;
    retired.cards = {
        Card::normal(CardId{10}, Suit::Spades, Rank::integer(2)),
        Card::normal(CardId{11}, Suit::Spades, Rank::integer(3)),
        Card::normal(CardId{12}, Suit::Spades, Rank::integer(4)),
        Card::normal(CardId{13}, Suit::Spades, Rank::integer(5)),
    };
    retired.hands[static_cast<int>(Seat::West)] = {CardId{10}};
    retired.hands[static_cast<int>(Seat::North)] = {CardId{11}};
    retired.hands[static_cast<int>(Seat::East)] = {CardId{12}};
    retired.hands[static_cast<int>(Seat::South)] = {CardId{13}};
    retired.next_player = Seat::West;
    retired.contract = Contract::NoTrump;
    retired.tricks_remaining = 1;
    retired.tricks_won = {1, 0, 0, 0};
    auto current = *Position::create(retired);

    EXPECT_EQ(played.hand(Seat::West), current.hand(Seat::West));
    EXPECT_NE(
        recompute_position_identity(played),
        recompute_position_identity(current));
}

TEST(PositionIdentityTest, WinCountsAreSeatSensitive)
{
    LivePosition base;
    base.cards = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(14)),
        Card::normal(CardId{2}, Suit::Spades, Rank::integer(13)),
        Card::normal(CardId{3}, Suit::Spades, Rank::integer(12)),
        Card::normal(CardId{4}, Suit::Spades, Rank::integer(11)),
    };
    base.hands[static_cast<int>(Seat::West)] = {CardId{1}};
    base.hands[static_cast<int>(Seat::North)] = {CardId{2}};
    base.hands[static_cast<int>(Seat::East)] = {CardId{3}};
    base.hands[static_cast<int>(Seat::South)] = {CardId{4}};
    base.next_player = Seat::West;
    base.contract = Contract::NoTrump;
    base.tricks_remaining = 2;
    base.tricks_won = {1, 0, 0, 0};

    LivePosition moved = base;
    moved.tricks_won = {0, 0, 1, 0};

    auto west_won = Position::create(base);
    auto east_won = Position::create(moved);
    ASSERT_TRUE(west_won.has_value());
    ASSERT_TRUE(east_won.has_value());
    // Same multiset of win counts, different owning seat: the identity must
    // still separate them, because the score depends on who won the trick.
    EXPECT_NE(
        recompute_position_identity(*west_won),
        recompute_position_identity(*east_won));
}

}  // namespace
}  // namespace touniu_solver
