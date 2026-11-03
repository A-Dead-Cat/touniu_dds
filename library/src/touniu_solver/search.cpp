#include "search.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <touniu_solver/position_hash.hpp>

namespace touniu_solver {
namespace {

auto partnership(const Seat seat) -> Partnership
{
    return seat == Seat::North || seat == Seat::South
        ? Partnership::NorthSouth : Partnership::EastWest;
}

auto score(const Position& position, const Partnership target) -> int
{
    if (target == Partnership::NorthSouth) {
        return static_cast<int>(
            position.tricks_won(Seat::North) + position.tricks_won(Seat::South));
    }
    return static_cast<int>(
        position.tricks_won(Seat::West) + position.tricks_won(Seat::East));
}

auto search_depth(const Position& position) -> std::uint32_t
{
    return position.tricks_remaining() * 4U -
           static_cast<std::uint32_t>(position.current_trick().size());
}

class SearchContext {
public:
    SearchContext(const Partnership target, const SearchOptions options)
        : target_(target), options_(options)
    {
    }

    auto solve(Position& position) -> SearchResult
    {
        if (position.is_finished()) {
            return {score(position, target_), std::nullopt};
        }

        const bool maximizing = partnership(position.next_player()) == target_;
        SearchResult result{
            maximizing ? std::numeric_limits<int>::min()
                       : std::numeric_limits<int>::max(),
            std::nullopt,
        };
        int alpha = std::numeric_limits<int>::min();
        int beta = std::numeric_limits<int>::max();
        for (const Action& action : position.legal_actions()) {
            if (!position.apply(action)) {
                continue;
            }
            const int value = evaluate(position, alpha, beta);
            position.undo();
            if (!result.best_action.has_value() ||
                (maximizing ? value > result.tricks : value < result.tricks)) {
                result.tricks = value;
                result.best_action = action;
            }
            if (maximizing) {
                alpha = std::max(alpha, result.tricks);
            } else {
                beta = std::min(beta, result.tricks);
            }
        }
        return result;
    }

    auto analyze(Position& position) -> std::vector<ActionAnalysis>
    {
        std::vector<ActionAnalysis> results;
        if (position.is_finished()) {
            return results;
        }
        const std::vector<Action> actions = position.legal_actions();
        results.reserve(actions.size());
        for (const Action& action : actions) {
            if (!position.apply(action)) {
                continue;
            }
            // Every displayed action gets its own full window so sibling
            // cutoffs can never leak into its value.
            const int value = evaluate(
                position,
                std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max());
            position.undo();
            results.push_back({action, value});
        }
        return results;
    }

private:
    auto evaluate(Position& position, int alpha, int beta) -> int
    {
        if (options_.stats != nullptr) {
            ++options_.stats->nodes;
        }
        if (position.is_finished()) {
            return score(position, target_);
        }

        const std::uint32_t depth = search_depth(position);
        const PositionIdentity identity = recompute_position_identity(position);
        const int caller_alpha = alpha;
        const int caller_beta = beta;
        const bool prune = options_.use_alpha_beta;
        const TranspositionEntry* cached = nullptr;

        if (options_.use_transposition_table) {
            if (options_.stats != nullptr) {
                ++options_.stats->table_queries;
            }
            const TranspositionTable::Probe probe = table_.probe(identity);
            if (probe.collision && options_.stats != nullptr) {
                ++options_.stats->collision_rejections;
            }
            cached = probe.entry;
            if (cached != nullptr && cached->depth >= depth) {
                if (options_.stats != nullptr) {
                    ++options_.stats->table_hits;
                }
                if (cached->exact.has_value()) {
                    return *cached->exact;
                }
                if (prune && cached->lower.has_value()) {
                    alpha = std::max(alpha, *cached->lower);
                }
                if (prune && cached->upper.has_value()) {
                    beta = std::min(beta, *cached->upper);
                }
                if (prune && alpha >= beta) {
                    if (options_.stats != nullptr) {
                        ++options_.stats->cutoffs;
                    }
                    return cached->lower.has_value() ? *cached->lower : *cached->upper;
                }
            }
        }

        const bool maximizing = partnership(position.next_player()) == target_;
        int best = maximizing ? std::numeric_limits<int>::min()
                              : std::numeric_limits<int>::max();
        std::optional<Action> best_action;
        std::vector<Action> actions = position.legal_actions();
        if (cached != nullptr && cached->best_action.has_value()) {
            const auto preferred = std::find(
                actions.begin(), actions.end(), *cached->best_action);
            if (preferred != actions.end()) {
                std::iter_swap(actions.begin(), preferred);
            }
        }

        for (const Action& action : actions) {
            if (!position.apply(action)) {
                continue;
            }
            const int value = evaluate(position, alpha, beta);
            position.undo();
            if (!best_action.has_value() ||
                (maximizing ? value > best : value < best)) {
                best = value;
                best_action = action;
            }
            if (maximizing) {
                alpha = std::max(alpha, best);
            } else {
                beta = std::min(beta, best);
            }
            if (prune && alpha >= beta) {
                if (options_.stats != nullptr) {
                    ++options_.stats->cutoffs;
                }
                break;
            }
        }

        if (options_.use_transposition_table) {
            std::optional<int> exact;
            std::optional<int> lower;
            std::optional<int> upper;
            if (!prune) {
                exact = best;
            } else if (best <= caller_alpha) {
                upper = best;
            } else if (best >= caller_beta) {
                lower = best;
            } else {
                exact = best;
            }
            table_.store(identity, depth, exact, lower, upper, best_action);
        }
        return best;
    }

    Partnership target_;
    SearchOptions options_;
    TranspositionTable table_;
};

}  // namespace

auto solve(Position& position, Partnership target, bool use_alpha_beta)
    -> SearchResult
{
    return solve(position, target, SearchOptions{use_alpha_beta, true, nullptr});
}

auto solve(Position& position, Partnership target, const SearchOptions& options)
    -> SearchResult
{
    SearchContext context(target, options);
    return context.solve(position);
}

auto analyze_actions(
    Position& position,
    Partnership target,
    bool use_alpha_beta) -> std::vector<ActionAnalysis>
{
    return analyze_actions(
        position, target, SearchOptions{use_alpha_beta, true, nullptr});
}

auto analyze_actions(
    Position& position,
    Partnership target,
    const SearchOptions& options) -> std::vector<ActionAnalysis>
{
    SearchContext context(target, options);
    return context.analyze(position);
}

}  // namespace touniu_solver
