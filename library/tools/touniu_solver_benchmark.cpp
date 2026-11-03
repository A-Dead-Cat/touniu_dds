#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <touniu_solver/position.hpp>
#include <touniu_solver/search.hpp>

namespace {

struct Args {
    int cards = 8;
    int samples = 3;
    std::uint32_t seed = 591601503U;
    bool legacy = false;
    bool contract_table = false;
    bool batch_only = false;
};

auto parse_uint(const char* text, std::uint32_t& out) -> bool
{
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

auto parse_args(const int argc, char** argv) -> Args
{
    Args args;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        const auto next = [&](std::uint32_t& out) {
            if (index + 1 >= argc) return false;
            return parse_uint(argv[++index], out);
        };
        std::uint32_t value = 0;
        if (flag == "--cards" && next(value)) {
            args.cards = static_cast<int>(value);
        } else if (flag == "--samples" && next(value)) {
            args.samples = static_cast<int>(value);
        } else if (flag == "--seed" && next(value)) {
            args.seed = value;
        } else if (flag == "--legacy") {
            args.legacy = true;
        } else if (flag == "--contract") {
            args.contract_table = true;
        } else if (flag == "--batch-only") {
            args.batch_only = true;
        }
    }
    if (args.cards < 1) args.cards = 1;
    if (args.cards > 13) args.cards = 13;
    if (args.samples < 1) args.samples = 1;
    return args;
}

auto make_deck() -> std::vector<touniu_solver::Card>
{
    using namespace touniu_solver;
    const std::array<Suit, 4> suits{
        Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs};
    std::vector<Card> deck;
    deck.reserve(52);
    std::uint64_t id = 1;
    for (const Suit suit : suits) {
        for (int rank = 2; rank <= 14; ++rank) {
            deck.push_back(Card::normal(CardId{id}, suit, Rank::integer(rank)));
            ++id;
        }
    }
    return deck;
}

auto xorshift32(std::uint32_t& state) -> std::uint32_t
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

auto build_scenario(const int cards, const std::uint32_t seed) -> touniu_solver::Scenario
{
    using namespace touniu_solver;
    std::vector<Card> deck = make_deck();
    std::uint32_t state = seed;
    for (int index = 51; index >= 1; --index) {
        const std::uint32_t swap =
            xorshift32(state) % static_cast<std::uint32_t>(index + 1);
        std::swap(deck[static_cast<std::size_t>(index)],
                  deck[static_cast<std::size_t>(swap)]);
    }

    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = static_cast<std::uint32_t>(cards);
    for (int index = 0; index < cards * 4; ++index) {
        scenario.hands[static_cast<std::size_t>(index % 4)].push_back(
            deck[static_cast<std::size_t>(index)]);
    }
    return scenario;
}

auto declarer_leader(const int declarer) -> touniu_solver::Seat
{
    using namespace touniu_solver;
    switch (declarer) {
        case 0:
            return Seat::East;
        case 1:
            return Seat::West;
        case 2:
            return Seat::South;
        default:
            return Seat::North;
    }
}

auto declarer_target(const int declarer) -> touniu_solver::Partnership
{
    using namespace touniu_solver;
    return declarer < 2 ? Partnership::NorthSouth : Partnership::EastWest;
}

auto strain_contract(const int strain) -> touniu_solver::Contract
{
    using namespace touniu_solver;
    switch (strain) {
        case 0:
            return Contract::NoTrump;
        case 1:
            return Contract::Spades;
        case 2:
            return Contract::Hearts;
        case 3:
            return Contract::Diamonds;
        default:
            return Contract::Clubs;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    using namespace touniu_solver;
    const Args args = parse_args(argc, argv);

    if (args.contract_table) {
        using namespace touniu_solver;
        std::vector<double> times_v2;
        std::vector<double> times_batch;
        std::vector<unsigned long long> nodes_v2_samples;
        std::vector<unsigned long long> nodes_batch_samples;
        unsigned long long total_v2 = 0;
        unsigned long long total_batch = 0;
        unsigned long long replacements_v2 = 0;
        unsigned long long replacements_batch = 0;
        SearchStats totals_v2;
        SearchStats totals_batch;
        int mismatches = 0;

        for (int sample = 0; sample < args.samples; ++sample) {
            const Scenario base = build_scenario(
                args.cards, args.seed + static_cast<std::uint32_t>(sample));
            std::vector<int> values_v2;
            std::vector<int> values_batch;
            unsigned long long sample_v2 = 0;
            unsigned long long sample_batch = 0;

            const auto start_v2 = std::chrono::steady_clock::now();
            if (!args.batch_only) {
            for (int strain = 0; strain < 5; ++strain) {
                for (int declarer = 0; declarer < 4; ++declarer) {
                    Scenario scenario = base;
                    scenario.contract = strain_contract(strain);
                    scenario.next_player = declarer_leader(declarer);
                    auto position = Position::create(scenario);
                    if (!position.has_value()) {
                        std::fprintf(stderr, "failed to create position\n");
                        return 1;
                    }
                    SearchStats stats;
                    values_v2.push_back(
                        solve(*position, declarer_target(declarer),
                              SearchOptions{true, true, &stats}).tricks);
                    sample_v2 += stats.nodes;
                    replacements_v2 += stats.table_replacements;
                    totals_v2.nodes += stats.nodes;
                    totals_v2.table_queries += stats.table_queries;
                    totals_v2.table_hits += stats.table_hits;
                    totals_v2.collision_rejections += stats.collision_rejections;
                    totals_v2.table_replacements += stats.table_replacements;
                }
            }
            }
            const auto end_v2 = std::chrono::steady_clock::now();

            const auto start_batch = std::chrono::steady_clock::now();
            for (int strain = 0; strain < 5; ++strain) {
                std::vector<ContractQuery> queries;
                queries.reserve(4);
                for (int declarer = 0; declarer < 4; ++declarer) {
                    Scenario scenario = base;
                    scenario.contract = strain_contract(strain);
                    scenario.next_player = declarer_leader(declarer);
                    auto position = Position::create(scenario);
                    if (!position.has_value()) {
                        std::fprintf(stderr, "failed to create position\n");
                        return 1;
                    }
                    queries.push_back(
                        {std::move(*position), declarer_target(declarer)});
                }
                SearchStats stats;
                const std::vector<int> values = solve_contract_batch(
                    queries, SearchOptions{true, true, &stats});
                sample_batch += stats.nodes;
                replacements_batch += stats.table_replacements;
                totals_batch.nodes += stats.nodes;
                totals_batch.table_queries += stats.table_queries;
                totals_batch.table_hits += stats.table_hits;
                totals_batch.collision_rejections += stats.collision_rejections;
                totals_batch.table_replacements += stats.table_replacements;
                values_batch.insert(values_batch.end(), values.begin(), values.end());
            }
            const auto end_batch = std::chrono::steady_clock::now();

            // Compare the same table cell: v2 is strain-major, batch is too.
            for (std::size_t index = 0; index < values_v2.size(); ++index) {
                if (values_v2[index] != values_batch[index]) {
                    ++mismatches;
                }
            }
            if (!args.batch_only) {
                times_v2.push_back(
                    std::chrono::duration<double, std::milli>(end_v2 - start_v2).count());
            }
            times_batch.push_back(
                std::chrono::duration<double, std::milli>(end_batch - start_batch).count());
            nodes_v2_samples.push_back(sample_v2);
            nodes_batch_samples.push_back(sample_batch);
            total_v2 += sample_v2;
            total_batch += sample_batch;
        }

        const auto average = [](const std::vector<double>& values) {
            double sum = 0.0;
            for (const double value : values) {
                sum += value;
            }
            return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
        };
        std::printf(
            "{\"cards\":%d,\"samples\":%d,\"seed\":%u,\"mode\":\"contract\","
            "\"average_ms_v2\":%.3f,\"average_ms_batch\":%.3f,"
            "\"nodes_v2\":%llu,\"nodes_batch\":%llu,\"mismatches\":%d,"
            "\"replacements_v2\":%llu,\"replacements_batch\":%llu,"
            "\"batch_only\":%s,"
            "\"queries_v2\":%llu,\"hits_v2\":%llu,\"collisions_v2\":%llu,"
            "\"queries_batch\":%llu,\"hits_batch\":%llu,\"collisions_batch\":%llu,"
            "\"samples_ms_v2\":[",
            args.cards,
            args.samples,
            args.seed,
            average(times_v2),
            average(times_batch),
            total_v2,
            total_batch,
            mismatches,
            replacements_v2,
            replacements_batch,
            args.batch_only ? "true" : "false",
            static_cast<unsigned long long>(totals_v2.table_queries),
            static_cast<unsigned long long>(totals_v2.table_hits),
            static_cast<unsigned long long>(totals_v2.collision_rejections),
            static_cast<unsigned long long>(totals_batch.table_queries),
            static_cast<unsigned long long>(totals_batch.table_hits),
            static_cast<unsigned long long>(totals_batch.collision_rejections));
        for (std::size_t index = 0; index < times_v2.size(); ++index) {
            std::printf("%s%.3f", index == 0 ? "" : ",", times_v2[index]);
        }
        std::printf("],\"samples_ms_batch\":[");
        for (std::size_t index = 0; index < times_batch.size(); ++index) {
            std::printf("%s%.3f", index == 0 ? "" : ",", times_batch[index]);
        }
        std::printf("],\"samples_nodes_v2\":[");
        for (std::size_t index = 0; index < nodes_v2_samples.size(); ++index) {
            std::printf("%s%llu", index == 0 ? "" : ",", nodes_v2_samples[index]);
        }
        std::printf("],\"samples_nodes_batch\":[");
        for (std::size_t index = 0; index < nodes_batch_samples.size(); ++index) {
            std::printf("%s%llu", index == 0 ? "" : ",", nodes_batch_samples[index]);
        }
        std::printf("]}\n");
        return mismatches == 0 ? 0 : 2;
    }

    double total_ms = 0.0;
    double minimum_ms = std::numeric_limits<double>::max();
    double maximum_ms = 0.0;
    SearchStats totals;
    std::vector<unsigned long long> sample_nodes;

    for (int sample = 0; sample < args.samples; ++sample) {
        const Scenario scenario = build_scenario(
            args.cards, args.seed + static_cast<std::uint32_t>(sample));
        auto position = Position::create(scenario);
        if (!position.has_value()) {
            std::fprintf(stderr, "failed to create position\n");
            return 1;
        }

        SearchStats stats;
        const auto start = std::chrono::steady_clock::now();
        std::vector<ActionAnalysis> analyses;
        if (args.legacy) {
            for (const Action& action : position->legal_actions()) {
                if (!position->apply(action)) continue;
                const SearchResult result = solve(
                    *position, Partnership::NorthSouth, SearchOptions{true, true, &stats});
                position->undo();
                analyses.push_back({action, result.tricks});
            }
        } else {
            analyses = analyze_actions(
                *position,
                Partnership::NorthSouth,
                SearchOptions{true, true, &stats});
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(end - start).count();

        if (analyses.empty()) {
            std::fprintf(stderr, "sample %d produced no legal actions\n", sample);
            return 1;
        }
        sample_nodes.push_back(
            static_cast<unsigned long long>(stats.nodes));
        total_ms += elapsed_ms;
        minimum_ms = std::min(minimum_ms, elapsed_ms);
        maximum_ms = std::max(maximum_ms, elapsed_ms);
        totals.nodes += stats.nodes;
        totals.table_queries += stats.table_queries;
        totals.table_hits += stats.table_hits;
        totals.collision_rejections += stats.collision_rejections;
        totals.cutoffs += stats.cutoffs;
        totals.pvs_null_windows += stats.pvs_null_windows;
        totals.pvs_researches += stats.pvs_researches;
        totals.equivalent_groups += stats.equivalent_groups;
        totals.equivalent_skips += stats.equivalent_skips;
        totals.table_replacements += stats.table_replacements;
    }

    const double average_ms = total_ms / args.samples;
    std::printf(
        "{\"cards\":%d,\"samples\":%d,\"seed\":%u,\"legacy\":%s,"
        "\"average_ms\":%.3f,\"min_ms\":%.3f,\"max_ms\":%.3f,"
        "\"nodes\":%llu,\"table_queries\":%llu,\"table_hits\":%llu,"
        "\"collision_rejections\":%llu,\"cutoffs\":%llu,"
        "\"pvs_null_windows\":%llu,\"pvs_researches\":%llu,"
        "\"equivalent_groups\":%llu,\"equivalent_skips\":%llu,"
        "\"table_replacements\":%llu,"
        "\"samples_nodes\":[",
        args.cards,
        args.samples,
        args.seed,
        args.legacy ? "true" : "false",
        average_ms,
        minimum_ms,
        maximum_ms,
        static_cast<unsigned long long>(totals.nodes),
        static_cast<unsigned long long>(totals.table_queries),
        static_cast<unsigned long long>(totals.table_hits),
        static_cast<unsigned long long>(totals.collision_rejections),
        static_cast<unsigned long long>(totals.cutoffs),
        static_cast<unsigned long long>(totals.pvs_null_windows),
        static_cast<unsigned long long>(totals.pvs_researches),
        static_cast<unsigned long long>(totals.equivalent_groups),
        static_cast<unsigned long long>(totals.equivalent_skips),
        static_cast<unsigned long long>(totals.table_replacements));
    for (std::size_t index = 0; index < sample_nodes.size(); ++index) {
        std::printf("%s%llu", index == 0 ? "" : ",", sample_nodes[index]);
    }
    std::printf("]}\n");
    return 0;
}
