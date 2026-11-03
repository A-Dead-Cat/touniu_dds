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

TEST(SearchTest, BoundedTableMatchesExhaustiveAcrossTwoTricks)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.tricks_remaining = 2;
    scenario.hands[0] = {normal(1, 14), normal(2, 2)};
    scenario.hands[1] = {normal(3, 13), normal(4, 3)};
    scenario.hands[2] = {normal(5, 12), normal(6, 4)};
    scenario.hands[3] = {normal(7, 11), normal(8, 5)};
    auto alpha_beta_position = *Position::create(scenario);
    auto exhaustive_position = alpha_beta_position;

    const SearchResult alpha_beta =
        solve(alpha_beta_position, Partnership::NorthSouth, true);
    const SearchResult exhaustive =
        solve(exhaustive_position, Partnership::NorthSouth, false);

    EXPECT_EQ(alpha_beta.tricks, exhaustive.tricks);
    EXPECT_EQ(alpha_beta.best_action, exhaustive.best_action);
    EXPECT_EQ(alpha_beta_position.hand(Seat::West).size(), 2U);
    EXPECT_TRUE(alpha_beta_position.current_trick().empty());
    EXPECT_EQ(alpha_beta_position.tricks_remaining(), 2U);
}

}  // namespace
}  // namespace touniu_solver
