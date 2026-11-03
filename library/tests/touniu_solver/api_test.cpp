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

TEST(ApiTest, AnalyzeActionsForcesEachLegalPlayAndSolvesItsContinuation)
{
    Scenario scenario;
    scenario.tricks_remaining = 1;
    scenario.contract = Contract::NoTrump;
    scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(14))};
    scenario.hands[2] = {
        Card::normal(CardId{2}, Suit::Hearts, Rank::integer(14))};
    scenario.next_player = Seat::West;
    auto position = *Position::create(scenario);

    const std::vector<ActionAnalysis> analyses =
        analyze_actions(position, Partnership::EastWest);

    ASSERT_EQ(analyses.size(), 1U);
    EXPECT_EQ(analyses.front().action.card_id(), CardId{1});
    // West leads the spade ace; East is void in spades and discards, so
    // East-West still win the only remaining trick.
    EXPECT_EQ(analyses.front().final_tricks, 1);
    EXPECT_EQ(position.hand(Seat::West).size(), 1U);
    EXPECT_EQ(position.hand(Seat::East).size(), 1U);
    EXPECT_EQ(position.current_trick().size(), 0U);
}

TEST(ApiTest, AnalyzeActionsForcingACompletingPlayRestoresThePosition)
{
    Scenario scenario;
    scenario.tricks_remaining = 1;
    scenario.contract = Contract::NoTrump;
    scenario.hands[0] = {Card::normal(CardId{1}, Suit::Spades, Rank::integer(14))};
    scenario.hands[1] = {Card::normal(CardId{2}, Suit::Spades, Rank::integer(13))};
    scenario.hands[2] = {Card::normal(CardId{3}, Suit::Spades, Rank::integer(12))};
    scenario.hands[3] = {Card::normal(CardId{4}, Suit::Spades, Rank::integer(11))};
    scenario.next_player = Seat::West;
    auto position = *Position::create(scenario);
    ASSERT_TRUE(position.apply(Action::play(CardId{1}, PlayMode::Lead, Suit::Spades)));
    ASSERT_TRUE(position.apply(Action::play(CardId{2}, PlayMode::Follow, Suit::Spades)));
    ASSERT_TRUE(position.apply(Action::play(CardId{3}, PlayMode::Follow, Suit::Spades)));
    ASSERT_EQ(position.current_trick().size(), 3U);

    const std::vector<ActionAnalysis> analyses =
        analyze_actions(position, Partnership::NorthSouth);

    ASSERT_EQ(analyses.size(), 1U);
    EXPECT_EQ(analyses.front().action.card_id(), CardId{4});
    // West's ace wins the completed trick, so North-South earn zero.
    EXPECT_EQ(analyses.front().final_tricks, 0);
    EXPECT_EQ(position.current_trick().size(), 3U);
    EXPECT_EQ(position.hand(Seat::South).size(), 1U);
}

}  // namespace
}  // namespace touniu_solver
