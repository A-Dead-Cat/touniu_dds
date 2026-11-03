#include <touniu_solver/live_position.hpp>
#include <touniu_solver/position.hpp>
#include <touniu_solver/position_hash.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace touniu_solver {
namespace {

auto normal_card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

auto valid_partial_position() -> LivePosition
{
    LivePosition live;
    live.cards = {
        normal_card(1, Suit::Spades, 14),
        normal_card(2, Suit::Spades, 13),
        normal_card(3, Suit::Hearts, 12),
        normal_card(4, Suit::Clubs, 11),
    };
    live.hands[static_cast<std::size_t>(Seat::East)] = {CardId{3}};
    live.hands[static_cast<std::size_t>(Seat::South)] = {CardId{4}};
    live.current_trick = {
        {Seat::West, false, CardId{1}, PlayMode::Lead, Suit::Spades},
        {Seat::North, false, CardId{2}, PlayMode::Follow, Suit::Spades},
    };
    live.next_player = Seat::East;
    live.contract = Contract::Spades;
    live.tricks_remaining = 3;
    live.tricks_won = {2, 1, 0, 4};
    return live;
}

TEST(LivePositionTest, ImportsPartialTrickAndAccumulatedWins)
{
    const LivePosition live = valid_partial_position();

    const auto position = Position::create(live);

    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->current_trick().size(), 2U);
    EXPECT_EQ(position->next_player(), Seat::East);
    EXPECT_EQ(position->tricks_remaining(), 3U);
    EXPECT_EQ(position->tricks_won(Seat::West), 2U);
    EXPECT_EQ(position->tricks_won(Seat::North), 1U);
    EXPECT_EQ(position->tricks_won(Seat::East), 0U);
    EXPECT_EQ(position->tricks_won(Seat::South), 4U);
}

TEST(LivePositionTest, RejectsHandCardMissingFromCatalog)
{
    LivePosition live = valid_partial_position();
    live.hands[static_cast<std::size_t>(Seat::East)] = {CardId{99}};

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsPlayedCardMissingFromCatalog)
{
    LivePosition live = valid_partial_position();
    live.current_trick.front().card_id = CardId{99};

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsUnusedCatalogCard)
{
    LivePosition live = valid_partial_position();
    live.cards.push_back(normal_card(5, Suit::Diamonds, 10));

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsDuplicateCatalogId)
{
    LivePosition live = valid_partial_position();
    live.cards.push_back(normal_card(1, Suit::Diamonds, 10));

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsCardBothHeldAndPlayed)
{
    LivePosition live = valid_partial_position();
    live.hands[static_cast<std::size_t>(Seat::East)].push_back(CardId{1});

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsCardOwnedByMultipleHands)
{
    LivePosition live = valid_partial_position();
    live.hands[static_cast<std::size_t>(Seat::West)].push_back(CardId{3});

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsPassWithNonzeroCardId)
{
    LivePosition live = valid_partial_position();
    live.current_trick[1] = {Seat::North, true, CardId{2}, std::nullopt, std::nullopt};

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsPassWithMode)
{
    LivePosition live = valid_partial_position();
    live.current_trick[1] = {
        Seat::North,
        true,
        CardId{0},
        PlayMode::Follow,
        std::nullopt,
    };

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsPassWithEffectiveSuit)
{
    LivePosition live = valid_partial_position();
    live.current_trick[1] = {
        Seat::North,
        true,
        CardId{0},
        std::nullopt,
        Suit::Spades,
    };

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsNonconsecutiveTrickSeats)
{
    LivePosition live = valid_partial_position();
    live.current_trick[1].seat = Seat::East;

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsNextPlayerThatDoesNotFollowPartialTrick)
{
    LivePosition live = valid_partial_position();
    live.next_player = Seat::South;

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsLeadModeAfterFirstPlay)
{
    LivePosition live = valid_partial_position();
    live.current_trick[1].mode = PlayMode::Lead;

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsInvalidPlayModeValue)
{
    LivePosition live = valid_partial_position();
    live.current_trick[1].mode = static_cast<PlayMode>(99);

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsInvalidEnumValues)
{
    LivePosition invalid_next = valid_partial_position();
    invalid_next.next_player = static_cast<Seat>(99);
    LivePosition invalid_direction = valid_partial_position();
    invalid_direction.direction = static_cast<Direction>(99);
    LivePosition invalid_contract = valid_partial_position();
    invalid_contract.contract = static_cast<Contract>(99);
    LivePosition invalid_trick_seat = valid_partial_position();
    invalid_trick_seat.current_trick.front().seat = static_cast<Seat>(99);
    LivePosition invalid_suit = valid_partial_position();
    invalid_suit.current_trick.back().effective_suit = static_cast<Suit>(99);

    EXPECT_FALSE(Position::create(invalid_next).has_value());
    EXPECT_FALSE(Position::create(invalid_direction).has_value());
    EXPECT_FALSE(Position::create(invalid_contract).has_value());
    EXPECT_FALSE(Position::create(invalid_trick_seat).has_value());
    EXPECT_FALSE(Position::create(invalid_suit).has_value());
}

TEST(LivePositionTest, RejectsInconsistentPendingBlock)
{
    LivePosition live = valid_partial_position();
    live.pending_block = Seat::East;

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, RejectsInconsistentValidReverseCount)
{
    LivePosition live = valid_partial_position();
    live.valid_reverse_count = 1;

    EXPECT_FALSE(Position::create(live).has_value());
}

TEST(LivePositionTest, AcceptsSeedExtremesAndNonzeroDrawIndex)
{
    LivePosition zero_seed = valid_partial_position();
    zero_seed.seed = 0;
    zero_seed.draw_index = 7;
    LivePosition maximum_seed = zero_seed;
    maximum_seed.seed = std::numeric_limits<std::uint32_t>::max();

    const auto zero = Position::create(zero_seed);
    const auto maximum = Position::create(maximum_seed);

    ASSERT_TRUE(zero.has_value());
    ASSERT_TRUE(maximum.has_value());
    EXPECT_EQ(zero->seed(), 0U);
    EXPECT_EQ(maximum->seed(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(zero->draw_index(), 7U);
    EXPECT_EQ(maximum->draw_index(), 7U);
}

TEST(LivePositionTest, ImportsAppliedEffectsWithoutReplayingAndUndoRestoresHash)
{
    LivePosition live;
    live.cards = {
        Card::plus_two(CardId{1}, Suit::Spades),
        Card::reverse(CardId{2}, Suit::Spades),
        Card::block(CardId{3}, Suit::Spades),
        normal_card(10, Suit::Hearts, 8),
        normal_card(11, Suit::Diamonds, 7),
        normal_card(12, Suit::Clubs, 6),
    };
    live.hands[static_cast<std::size_t>(Seat::North)] = {CardId{10}, CardId{11}};
    live.hands[static_cast<std::size_t>(Seat::South)] = {CardId{12}};
    live.current_trick = {
        {Seat::West, false, CardId{1}, PlayMode::Lead, Suit::Spades},
        {Seat::North, false, CardId{2}, PlayMode::Follow, Suit::Spades},
        {Seat::East, false, CardId{3}, PlayMode::Follow, Suit::Spades},
    };
    live.next_player = Seat::South;
    live.contract = Contract::Spades;
    live.tricks_remaining = 2;
    live.tricks_won = {1, 2, 3, 4};
    live.pending_block = Seat::South;
    live.valid_reverse_count = 1;
    live.seed = 0;
    live.draw_index = 2;

    auto position = Position::create(live);

    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->hand(Seat::North).size(), 2U);
    EXPECT_EQ(position->draw_index(), 2U);
    EXPECT_EQ(position->direction(), Direction::Forward);
    EXPECT_EQ(position->valid_reverse_count(), 1U);
    EXPECT_EQ(position->pending_block(), Seat::South);
    EXPECT_EQ(position->tricks_remaining(), 2U);
    const std::uint64_t imported_hash = hash_position(*position);

    const std::vector<Action> actions = position->legal_actions();
    ASSERT_EQ(actions.size(), 1U);
    ASSERT_TRUE(actions.front().is_pass());
    ASSERT_TRUE(position->apply(actions.front()));
    position->undo();

    EXPECT_EQ(hash_position(*position), imported_hash);
    EXPECT_EQ(position->hand(Seat::North).size(), 2U);
    EXPECT_EQ(position->draw_index(), 2U);
    EXPECT_EQ(position->pending_block(), Seat::South);
}

TEST(LivePositionTest, DiscardedEffectCardsDoNotContributePendingEffects)
{
    LivePosition live = valid_partial_position();
    live.cards[1] = Card::reverse(CardId{2}, Suit::Hearts);
    live.current_trick[1] = {
        Seat::North,
        false,
        CardId{2},
        PlayMode::Discard,
        Suit::NoTrump,
    };
    live.valid_reverse_count = 0;

    EXPECT_TRUE(Position::create(live).has_value());
}

}  // namespace
}  // namespace touniu_solver
