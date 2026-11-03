#include <touniu_solver/search.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

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
    EXPECT_GT(stats.pvs_null_windows, 0U);

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

auto check_seeded_deals(const SearchOptions& options) -> void
{
    // 16 normal cards: four ranks in four suits, dealt four per seat. Different
    // play orders reach positions whose only difference is which seat of a
    // partnership won earlier tricks, so this also exercises the win-count
    // component of the maintained identity.
    constexpr std::array<Suit, 4> suits = {
        Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs};
    constexpr std::array<int, 4> ranks = {14, 13, 12, 11};

    std::uint32_t random_state = 0x9E3779B9U;
    const auto next_random = [&random_state]() {
        random_state ^= random_state << 13U;
        random_state ^= random_state >> 17U;
        random_state ^= random_state << 5U;
        return random_state;
    };

    for (int deal_index = 0; deal_index < 4; ++deal_index) {
        std::vector<Card> deck;
        std::uint64_t id = 1;
        for (const Suit suit : suits) {
            for (const int rank : ranks) {
                deck.push_back(Card::normal(CardId{id++}, suit, Rank::integer(rank)));
            }
        }
        for (std::size_t index = deck.size() - 1; index > 0; --index) {
            std::swap(deck[index], deck[next_random() % (index + 1)]);
        }

        Scenario scenario;
        scenario.next_player = Seat::West;
        scenario.contract = Contract::NoTrump;
        scenario.tricks_remaining = 4;
        for (std::size_t seat = 0; seat < 4; ++seat) {
            for (std::size_t card = 0; card < 4; ++card) {
                scenario.hands[seat].push_back(deck[seat * 4 + card]);
            }
        }

        auto position = *Position::create(scenario);
        const std::vector<ActionAnalysis> analyses =
            analyze_actions(position, Partnership::NorthSouth, options);

        ASSERT_FALSE(analyses.empty());
        for (const ActionAnalysis& analysis : analyses) {
            auto forced = *Position::create(scenario);
            ASSERT_TRUE(forced.apply(analysis.action));
            EXPECT_EQ(analysis.final_tricks, oracle(forced, Partnership::NorthSouth))
                << "deal " << deal_index;
        }
    }
}

auto ordered_scenario(const int deal_index) -> Scenario
{
    constexpr std::array<Suit, 4> suits = {
        Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs};
    constexpr std::array<int, 4> ranks = {14, 13, 12, 11};
    std::uint32_t random_state =
        0x9E3779B9U + static_cast<std::uint32_t>(deal_index) * 2654435761U;
    const auto next_random = [&random_state]() {
        random_state ^= random_state << 13U;
        random_state ^= random_state >> 17U;
        random_state ^= random_state << 5U;
        return random_state;
    };

    std::vector<Card> deck;
    std::uint64_t id = 1;
    for (const Suit suit : suits) {
        for (const int rank : ranks) {
            deck.push_back(Card::normal(CardId{id++}, suit, Rank::integer(rank)));
        }
    }
    for (std::size_t index = deck.size() - 1; index > 0; --index) {
        std::swap(deck[index], deck[next_random() % (index + 1)]);
    }

    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 4;
    for (std::size_t seat = 0; seat < 4; ++seat) {
        for (std::size_t card = 0; card < 4; ++card) {
            scenario.hands[seat].push_back(deck[seat * 4 + card]);
        }
    }
    return scenario;
}

auto lead_action(const std::uint64_t id) -> Action
{
    return Action::play(CardId{id}, PlayMode::Lead, Suit::Spades);
}

auto equivalence_scenario() -> Scenario
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(9)),
        Card::normal(CardId{2}, Suit::Spades, Rank::integer(8)),
    };
    scenario.hands[1] = {
        Card::normal(CardId{3}, Suit::Hearts, Rank::integer(5)),
    };
    return scenario;
}

TEST(SearchTest, EquivalentCardsShareAResult)
{
    const auto position = Position::create(equivalence_scenario());
    ASSERT_TRUE(position.has_value());
    // Adjacent spade ranks with no card strictly between them.
    EXPECT_TRUE(actions_are_equivalent(*position, lead_action(1), lead_action(2)));
}

TEST(SearchTest, EquivalentPredicateRejectsGapsAndMismatches)
{
    Scenario gapped = equivalence_scenario();
    gapped.hands[1] = {
        Card::normal(CardId{3}, Suit::Spades, Rank::integer(8)),
        Card::normal(CardId{4}, Suit::Hearts, Rank::integer(5)),
    };
    // A spade 8 lies strictly between ranks 8 and 9 only if the candidates are
    // 9 and 7; use a hand with 9 and 7 plus an intervening 8.
    gapped.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(9)),
        Card::normal(CardId{2}, Suit::Spades, Rank::integer(7)),
    };
    const auto gapped_position = Position::create(gapped);
    ASSERT_TRUE(gapped_position.has_value());
    EXPECT_FALSE(actions_are_equivalent(*gapped_position, lead_action(1), lead_action(2)));

    // Different suits never merge.
    Scenario suits = equivalence_scenario();
    suits.hands[0][1] = Card::normal(CardId{2}, Suit::Hearts, Rank::integer(8));
    const auto suits_position = Position::create(suits);
    ASSERT_TRUE(suits_position.has_value());
    EXPECT_FALSE(actions_are_equivalent(*suits_position, lead_action(1), lead_action(2)));

    // A remaining +2 could generate a rank between the candidates.
    Scenario plus_two = equivalence_scenario();
    plus_two.hands[1] = {
        Card::plus_two(CardId{3}, Suit::Spades),
    };
    const auto plus_two_position = Position::create(plus_two);
    ASSERT_TRUE(plus_two_position.has_value());
    EXPECT_FALSE(actions_are_equivalent(*plus_two_position, lead_action(1), lead_action(2)));

    // Multi-suit specials always block the merge.
    Scenario special = equivalence_scenario();
    special.hands[1] = {
        Card::four_suit(CardId{3}, Rank::integer(6)),
    };
    const auto special_position = Position::create(special);
    ASSERT_TRUE(special_position.has_value());
    EXPECT_FALSE(actions_are_equivalent(*special_position, lead_action(1), lead_action(2)));
}

TEST(SearchTest, EquivalentPruningKeepsExactValuesAndLowersNodes)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(9)),
        Card::normal(CardId{2}, Suit::Spades, Rank::integer(8)),
    };
    scenario.hands[1] = {
        Card::normal(CardId{3}, Suit::Spades, Rank::integer(14)),
        Card::normal(CardId{4}, Suit::Spades, Rank::integer(13)),
    };
    scenario.hands[2] = {
        Card::normal(CardId{5}, Suit::Spades, Rank::integer(12)),
        Card::normal(CardId{6}, Suit::Spades, Rank::integer(11)),
    };
    scenario.hands[3] = {
        Card::normal(CardId{7}, Suit::Spades, Rank::integer(10)),
        Card::normal(CardId{8}, Suit::Spades, Rank::integer(7)),
    };

    auto pruned = *Position::create(scenario);
    auto plain = *Position::create(scenario);
    SearchStats pruned_stats;
    SearchStats plain_stats;
    const SearchResult pruned_result = solve(
        pruned, Partnership::NorthSouth, SearchOptions{true, true, &pruned_stats});
    const SearchResult plain_result = solve(
        plain,
        Partnership::NorthSouth,
        SearchOptions{true, true, &plain_stats, true, false});

    EXPECT_EQ(pruned_result.tricks, plain_result.tricks);
    EXPECT_EQ(pruned_result.best_action, plain_result.best_action);
    EXPECT_GT(pruned_stats.equivalent_skips, 0U);
    EXPECT_LT(pruned_stats.nodes, plain_stats.nodes);
}

TEST(SearchTest, EquivalentPruningKeepsEveryRootAction)
{
    const Scenario scenario = equivalence_scenario();
    auto pruned = *Position::create(scenario);
    auto plain = *Position::create(scenario);
    const std::vector<ActionAnalysis> pruned_actions = analyze_actions(
        pruned, Partnership::NorthSouth, SearchOptions{true, true, nullptr});
    const std::vector<ActionAnalysis> plain_actions = analyze_actions(
        plain,
        Partnership::NorthSouth,
        SearchOptions{true, true, nullptr, true, false});

    ASSERT_EQ(pruned_actions.size(), plain_actions.size());
    for (std::size_t index = 0; index < pruned_actions.size(); ++index) {
        EXPECT_EQ(pruned_actions[index].action, plain_actions[index].action);
        EXPECT_EQ(pruned_actions[index].final_tricks, plain_actions[index].final_tricks);
    }
}

auto contract_scenario(const Seat leader) -> Scenario
{
    Scenario scenario;
    scenario.next_player = leader;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 3;
    scenario.hands[0] = {
        Card::normal(CardId{1}, Suit::Spades, Rank::integer(14)),
        Card::normal(CardId{2}, Suit::Spades, Rank::integer(2)),
        Card::normal(CardId{3}, Suit::Hearts, Rank::integer(5)),
    };
    scenario.hands[1] = {
        Card::normal(CardId{4}, Suit::Spades, Rank::integer(13)),
        Card::normal(CardId{5}, Suit::Hearts, Rank::integer(14)),
        Card::normal(CardId{6}, Suit::Clubs, Rank::integer(3)),
    };
    scenario.hands[2] = {
        Card::normal(CardId{7}, Suit::Spades, Rank::integer(12)),
        Card::normal(CardId{8}, Suit::Hearts, Rank::integer(13)),
        Card::normal(CardId{9}, Suit::Diamonds, Rank::integer(4)),
    };
    scenario.hands[3] = {
        Card::normal(CardId{10}, Suit::Spades, Rank::integer(11)),
        Card::normal(CardId{11}, Suit::Hearts, Rank::integer(12)),
        Card::normal(CardId{12}, Suit::Clubs, Rank::integer(5)),
    };
    return scenario;
}

TEST(SearchTest, ContractBatchMatchesIndependentSolves)
{
    // Declarer -> (leader, target) follows the both-directions contract table.
    struct Entry {
        Seat declarer;
        Seat leader;
        Partnership target;
    };
    const std::array<Entry, 4> entries = {{
        {Seat::North, Seat::East, Partnership::NorthSouth},
        {Seat::South, Seat::West, Partnership::NorthSouth},
        {Seat::East, Seat::South, Partnership::EastWest},
        {Seat::West, Seat::North, Partnership::EastWest},
    }};

    std::vector<ContractQuery> queries;
    std::vector<int> independent;
    for (const Entry& entry : entries) {
        auto position = Position::create(contract_scenario(entry.leader));
        ASSERT_TRUE(position.has_value());
        auto reference = Position::create(contract_scenario(entry.leader));
        ASSERT_TRUE(reference.has_value());
        independent.push_back(solve(*reference, entry.target).tricks);
        queries.push_back({std::move(*position), entry.target});
    }

    const std::vector<int> shared = solve_contract_batch(queries);
    ASSERT_EQ(shared.size(), independent.size());
    for (std::size_t index = 0; index < shared.size(); ++index) {
        EXPECT_EQ(shared[index], independent[index]) << "declarer " << index;
    }

    // The same position viewed from both partnerships is complementary.
    auto north_south_position = Position::create(contract_scenario(Seat::East));
    auto east_west_position = Position::create(contract_scenario(Seat::East));
    ASSERT_TRUE(north_south_position.has_value());
    ASSERT_TRUE(east_west_position.has_value());
    const int north_south =
        solve(*north_south_position, Partnership::NorthSouth).tricks;
    const int east_west =
        solve(*east_west_position, Partnership::EastWest).tricks;
    EXPECT_EQ(north_south + east_west, 3);
}

TEST(SearchTest, ContractBatchSharesTranspositionHits)
{
    std::vector<ContractQuery> batch;
    for (const Seat leader : {Seat::East, Seat::West, Seat::South, Seat::North}) {
        auto position = Position::create(contract_scenario(leader));
        ASSERT_TRUE(position.has_value());
        const Partnership target = leader == Seat::East || leader == Seat::West
            ? Partnership::NorthSouth : Partnership::EastWest;
        batch.push_back({std::move(*position), target});
    }
    SearchStats shared_stats;
    solve_contract_batch(batch, SearchOptions{true, true, &shared_stats});

    std::uint64_t independent_nodes = 0;
    for (const Seat leader : {Seat::East, Seat::West, Seat::South, Seat::North}) {
        auto position = Position::create(contract_scenario(leader));
        ASSERT_TRUE(position.has_value());
        const Partnership target = leader == Seat::East || leader == Seat::West
            ? Partnership::NorthSouth : Partnership::EastWest;
        SearchStats stats;
        solve(*position, target, SearchOptions{true, true, &stats});
        independent_nodes += stats.nodes;
    }

    EXPECT_GT(shared_stats.nodes, 0U);
    EXPECT_LT(shared_stats.nodes, independent_nodes);
}

TEST(SearchTest, EastWestActionValuesMatchTheOracle)
{
    const Scenario scenario = contract_scenario(Seat::South);
    auto position = *Position::create(scenario);
    const std::vector<ActionAnalysis> analyses = analyze_actions(
        position, Partnership::EastWest, SearchOptions{true, true, nullptr});

    ASSERT_FALSE(analyses.empty());
    for (const ActionAnalysis& analysis : analyses) {
        auto forced = *Position::create(scenario);
        ASSERT_TRUE(forced.apply(analysis.action));
        EXPECT_EQ(analysis.final_tricks, oracle(forced, Partnership::EastWest));
    }
}

TEST(SearchTest, DeterministicOrderingKeepsCanonicalBestAction)
{
    for (int deal_index = 0; deal_index < 4; ++deal_index) {
        const Scenario scenario = ordered_scenario(deal_index);
        auto ordered = *Position::create(scenario);
        auto canonical = *Position::create(scenario);

        const SearchResult fast = solve(
            ordered, Partnership::NorthSouth, SearchOptions{true, true, nullptr});
        const SearchResult exhaustive = solve(
            canonical, Partnership::NorthSouth, SearchOptions{false, false, nullptr});

        EXPECT_EQ(fast.tricks, exhaustive.tricks) << "deal " << deal_index;
        // Ordering only changes search order; an equal-valued tie must still
        // resolve to the earliest action in the canonical legal list.
        EXPECT_EQ(fast.best_action, exhaustive.best_action) << "deal " << deal_index;
    }
}

TEST(SearchTest, SharedActionAnalysisMatchesOracleOnSeededDeals)
{
    check_seeded_deals(SearchOptions{true, true, nullptr, false});
}

TEST(SearchTest, MtdActionAnalysisMatchesOracleOnSeededDeals)
{
    check_seeded_deals(SearchOptions{true, true, nullptr, true});
}

}  // namespace
}  // namespace touniu_solver
