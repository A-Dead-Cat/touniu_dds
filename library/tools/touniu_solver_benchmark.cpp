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

}  // namespace

int main(int argc, char** argv)
{
    using namespace touniu_solver;
    const Args args = parse_args(argc, argv);

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
    }

    const double average_ms = total_ms / args.samples;
    std::printf(
        "{\"cards\":%d,\"samples\":%d,\"seed\":%u,\"legacy\":%s,"
        "\"average_ms\":%.3f,\"min_ms\":%.3f,\"max_ms\":%.3f,"
        "\"nodes\":%llu,\"table_queries\":%llu,\"table_hits\":%llu,"
        "\"collision_rejections\":%llu,\"cutoffs\":%llu,"
        "\"pvs_null_windows\":%llu,\"pvs_researches\":%llu,"
        "\"equivalent_groups\":%llu,\"equivalent_skips\":%llu,"
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
        static_cast<unsigned long long>(totals.equivalent_skips));
    for (std::size_t index = 0; index < sample_nodes.size(); ++index) {
        std::printf("%s%llu", index == 0 ? "" : ",", sample_nodes[index]);
    }
    std::printf("]}\n");
    return 0;
}
