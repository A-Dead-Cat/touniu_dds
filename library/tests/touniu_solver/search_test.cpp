#include <touniu_solver/search.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace touniu_solver {
namespace {

auto normal(const std::uint64_t id, const int rank) -> Card
{
    return Card::normal(CardId{id}, Suit::Spades, Rank::integer(rank));
}

auto target_of(const Seat seat) -> Partnership
{
    return seat == Seat::North || seat == Seat::South
        ? Partnership::NorthSouth : Partnership::EastWest;
}

auto score_of(const Position& position, const Partnership target) -> int
{
    if (target == Partnership::NorthSouth) {
        return static_cast<int>(
            position.tricks_won(Seat::North) + position.tricks_won(Seat::South));
    }
    return static_cast<int>(
        position.tricks_won(Seat::West) + position.tricks_won(Seat::East));
}

// TT-free, alpha-beta-free reference minimax used only by tests.
auto oracle(Position& position, const Partnership target) -> int
{
    if (position.is_finished()) {
        return score_of(position, target);
    }
    const bool maximizing = target_of(position.next_player()) == target;
    int best = maximizing ? std::numeric_limits<int>::min()
                          : std::numeric_limits<int>::max();
    for (const Action& action : position.legal_actions()) {
        if (!position.apply(action)) {
            continue;
        }
        const int value = oracle(position, target);
        position.undo();
        best = maximizing ? std::max(best, value) : std::min(best, value);
    }
    return best;
}

auto shared_scenario() -> Scenario
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 3;
    scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(14)),
        Card::normal(CardId{2}, Suit::Hearts, Rank::integer(14)),
        Card::normal(CardId{3}, Suit::Clubs, Rank::integer(14)),
    };
    scenario.hands[1] = {
        Card::normal(CardId{4}, Suit::Spades, Rank::integer(13)),
        Card::normal(CardId{5}, Suit::Hearts, Rank::integer(13)),
        Card::normal(CardId{6}, Suit::Clubs, Rank::integer(13)),
    };
    scenario.hands[2] = {
        Card::normal(CardId{7}, Suit::Spades, Rank::integer(12)),
        Card::normal(CardId{8}, Suit::Hearts, Rank::integer(12)),
        Card::normal(CardId{9}, Suit::Clubs, Rank::integer(12)),
    };
    scenario.hands[3] = {
        Card::normal(CardId{10}, Suit::Spades, Rank::integer(11)),
        Card::normal(CardId{11}, Suit::Hearts, Rank::integer(11)),
        Card::normal(CardId{12}, Suit::Clubs, Rank::integer(11)),
    };
    return scenario;
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

TEST(SearchTest, SharedActionAnalysisMatchesTheOracle)
{
    const Scenario scenario = shared_scenario();
    auto position = *Position::create(scenario);
    SearchStats stats;

    const std::vector<ActionAnalysis> analyses = analyze_actions(
        position,
        Partnership::NorthSouth,
        SearchOptions{true, true, &stats});

    ASSERT_FALSE(analyses.empty());
    EXPECT_GT(stats.nodes, 0U);
    EXPECT_GT(stats.table_queries, 0U);
    EXPECT_GT(stats.table_hits, 0U);

    for (const ActionAnalysis& analysis : analyses) {
        auto forced = *Position::create(scenario);
        ASSERT_TRUE(forced.apply(analysis.action));
        EXPECT_EQ(analysis.final_tricks, oracle(forced, Partnership::NorthSouth));
    }

    EXPECT_TRUE(position.current_trick().empty());
    EXPECT_EQ(position.tricks_remaining(), 3U);
}

TEST(SearchTest, SharedActionAnalysisIsExactWithoutAlphaBeta)
{
    const Scenario scenario = shared_scenario();
    auto position = *Position::create(scenario);
    auto reference = *Position::create(scenario);

    const std::vector<ActionAnalysis> analyses = analyze_actions(
        position, Partnership::NorthSouth, SearchOptions{true, true, nullptr});
    const std::vector<ActionAnalysis> exhaustive = analyze_actions(
        reference, Partnership::NorthSouth, SearchOptions{false, false, nullptr});

    ASSERT_EQ(analyses.size(), exhaustive.size());
    for (std::size_t index = 0; index < analyses.size(); ++index) {
        EXPECT_EQ(analyses[index].action, exhaustive[index].action);
        EXPECT_EQ(analyses[index].final_tricks, exhaustive[index].final_tricks);
    }
}

TEST(SearchTest, SharedActionAnalysisHandlesPartialTricksAndEffects)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[0] = {Card::plus_two(CardId{1}, Suit::Spades), normal(2, 9)};
    scenario.hands[1] = {normal(3, 13), normal(4, 3)};
    scenario.hands[2] = {normal(5, 12), normal(6, 4)};
    scenario.hands[3] = {normal(7, 11), normal(8, 5)};
    auto position = *Position::create(scenario);
    const std::vector<Action> leads = position.legal_actions();
    const auto plus_two = std::find_if(leads.begin(), leads.end(), [](const Action& action) {
        return action.card_id() == CardId{1};
    });
    ASSERT_NE(plus_two, leads.end());
    ASSERT_TRUE(position.apply(*plus_two));

    const std::vector<ActionAnalysis> analyses = analyze_actions(
        position, Partnership::NorthSouth, SearchOptions{true, true, nullptr});

    ASSERT_FALSE(analyses.empty());
    for (const ActionAnalysis& analysis : analyses) {
        auto forced = *Position::create(scenario);
        ASSERT_TRUE(forced.apply(*plus_two));
        ASSERT_TRUE(forced.apply(analysis.action));
        EXPECT_EQ(analysis.final_tricks, oracle(forced, Partnership::NorthSouth));
    }
    EXPECT_EQ(position.current_trick().size(), 1U);
}

}  // namespace
}  // namespace touniu_solver
