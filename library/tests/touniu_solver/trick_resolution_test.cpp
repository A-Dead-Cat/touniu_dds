#include <touniu_solver/trick_resolution.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

auto state_with(std::vector<Card> cards, const Contract contract = Contract::NoTrump)
    -> State
{
    Scenario scenario;
    scenario.contract = contract;
    scenario.direction = Direction::Forward;
    scenario.hands[0] = std::move(cards);
    return *State::create(scenario);
}

TEST(TrickResolutionTest, EarlierEqualMaximumWins)
{
    const State state = state_with({card(1, Suit::Spades, 10), card(2, Suit::Spades, 10)});
    const Trick trick{
        {Seat::West, Action::play(CardId{1}, PlayMode::Lead, Suit::Spades)},
        {Seat::North, Action::play(CardId{2}, PlayMode::Follow, Suit::Spades)},
        {Seat::East, Action::pass()}, {Seat::South, Action::pass()},
    };

    EXPECT_EQ(resolve_trick(state, trick).winner, Seat::West);
}

TEST(TrickResolutionTest, SuitTrumpBeatsHigherLedSuit)
{
    const State state = state_with(
        {card(1, Suit::Spades, 14), card(2, Suit::Hearts, 2)}, Contract::Hearts);
    const Trick trick{
        {Seat::West, Action::play(CardId{1}, PlayMode::Lead, Suit::Spades)},
        {Seat::North, Action::play(CardId{2}, PlayMode::Trump, Suit::Hearts)},
        {Seat::East, Action::pass()}, {Seat::South, Action::pass()},
    };

    EXPECT_EQ(resolve_trick(state, trick).winner, Seat::North);
}

TEST(TrickResolutionTest, CherryRedirectsWinnerToNextDirectionalSeat)
{
    const State state = state_with({Card::cherry_bomb(CardId{1}, Suit::Spades)});
    const Trick trick{
        {Seat::West, Action::play(CardId{1}, PlayMode::Lead, Suit::Spades)},
        {Seat::North, Action::pass()}, {Seat::East, Action::pass()}, {Seat::South, Action::pass()},
    };

    EXPECT_EQ(resolve_trick(state, trick).winner, Seat::North);
}

TEST(TrickResolutionTest, EffectsRespectModeAndFourthHandRules)
{
    const State state = state_with({
        Card::block(CardId{1}, Suit::Spades),
        Card::reverse(CardId{2}, Suit::Spades),
        Card::plus_two(CardId{3}, Suit::Spades),
        Card::plus_two(CardId{4}, Suit::Spades),
    });
    const Trick trick{
        {Seat::West, Action::play(CardId{1}, PlayMode::Discard, Suit::Spades)},
        {Seat::North, Action::play(CardId{2}, PlayMode::Follow, Suit::Spades)},
        {Seat::East, Action::play(CardId{3}, PlayMode::Follow, Suit::Spades)},
        {Seat::South, Action::play(CardId{4}, PlayMode::Follow, Suit::Spades)},
    };

    const TrickResult result = resolve_trick(state, trick);
    EXPECT_FALSE(result.forced_pass.has_value());
    EXPECT_EQ(result.valid_reverse_count, 1U);
    ASSERT_EQ(result.draws.size(), 2U);
    EXPECT_EQ(result.draws[0].seat, Seat::South);
    EXPECT_EQ(result.draws[1].seat, Seat::West);
}

TEST(TrickResolutionTest, PassLeadAllowsAnySuitButTrumpStillHasPriority)
{
    const State state = state_with(
        {card(1, Suit::Spades, 14), card(2, Suit::Hearts, 2)}, Contract::Hearts);
    const Trick trick{
        {Seat::West, Action::pass()},
        {Seat::North, Action::play(CardId{1}, PlayMode::Discard, Suit::Spades)},
        {Seat::East, Action::play(CardId{2}, PlayMode::Trump, Suit::Hearts)},
        {Seat::South, Action::pass()},
    };

    EXPECT_EQ(resolve_trick(state, trick).winner, Seat::East);
}

TEST(TrickResolutionTest, FourPassesAwardTheTrickToTheFirstSeat)
{
    const State state = state_with({});
    const Trick trick{
        {Seat::West, Action::pass()}, {Seat::North, Action::pass()},
        {Seat::East, Action::pass()}, {Seat::South, Action::pass()},
    };

    EXPECT_EQ(resolve_trick(state, trick).winner, Seat::West);
}

TEST(TrickResolutionTest, CatRankDowngradesToPointNineNineNineNineNineNineInNoTrump)
{
    const State state = state_with({
        Card::three_suit_cat(CardId{1}), card(2, Suit::Spades, 1)});
    const Trick trick{
        {Seat::West, Action::play(CardId{1}, PlayMode::Lead, Suit::NoTrump)},
        {Seat::North, Action::play(CardId{2}, PlayMode::Discard, Suit::Spades)},
        {Seat::East, Action::pass()}, {Seat::South, Action::pass()},
    };

    EXPECT_EQ(resolve_trick(state, trick).winner, Seat::North);
}

}  // namespace
}  // namespace touniu_solver
