#include <touniu_solver/api.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

TEST(ApiTest, SolvesAValidScenarioWithoutMutatingInput)
{
    SolveRequest request;
    request.target = Partnership::EastWest;
    request.scenario.tricks_remaining = 1;
    request.scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(14))};
    const Scenario before = request.scenario;

    const SolveResponse response = solve_scenario(request);

    EXPECT_EQ(response.error, SolveError::None);
    ASSERT_TRUE(response.result.has_value());
    EXPECT_EQ(response.result->tricks, 1);
    EXPECT_EQ(request.scenario.hands[0].front().id(), before.hands[0].front().id());
}

TEST(ApiTest, RejectsDuplicateEntityIds)
{
    SolveRequest request;
    request.scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(14))};
    request.scenario.hands[1] = {
        Card::normal(CardId{1}, Suit::Hearts, Rank::integer(2))};

    const SolveResponse response = solve_scenario(request);

    EXPECT_EQ(response.error, SolveError::InvalidScenario);
    EXPECT_FALSE(response.result.has_value());
}

}  // namespace
}  // namespace touniu_solver
