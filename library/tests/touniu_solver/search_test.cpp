#include <touniu_solver/search.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto normal(const std::uint64_t id, const int rank) -> Card
{
    return Card::normal(CardId{id}, Suit::Spades, Rank::integer(rank));
}

TEST(SearchTest, SelectsLeadThatWinsTheOnlyTrickForEastWest)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[0] = {normal(1, 14), normal(2, 2)};
    scenario.hands[1] = {normal(3, 13)};
    auto position = *Position::create(scenario);

    const SearchResult result = solve(position, Partnership::EastWest);

    EXPECT_EQ(result.tricks, 1);
    ASSERT_TRUE(result.best_action.has_value());
    EXPECT_EQ(result.best_action->card_id(), CardId{1});
}

TEST(SearchTest, AlphaBetaMatchesExhaustiveMinimax)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.tricks_remaining = 1;
    scenario.hands[0] = {normal(1, 14), normal(2, 2)};
    scenario.hands[1] = {normal(3, 13)};
    auto first = *Position::create(scenario);
    auto second = first;

    const SearchResult alpha_beta = solve(first, Partnership::NorthSouth, true);
    const SearchResult exhaustive = solve(second, Partnership::NorthSouth, false);

    EXPECT_EQ(alpha_beta.tricks, exhaustive.tricks);
    EXPECT_EQ(alpha_beta.best_action, exhaustive.best_action);
}

}  // namespace
}  // namespace touniu_solver
