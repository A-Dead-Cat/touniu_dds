#include "search.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <array>
#include <optional>
#include <utility>
#include <vector>

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

// Flat-table capacity per deal size. The values cover the observed number of
// distinct positions with headroom while staying well inside a wasm32 heap.
auto transposition_slots(const std::uint32_t tricks) -> std::size_t
{
    if (tricks <= 5U) {
        return 1U << 15U;
    }
    if (tricks == 6U) {
        return 1U << 17U;
    }
    if (tricks == 7U) {
        return 1U << 20U;
    }
    if (tricks == 8U) {
        return 1U << 23U;
    }
    return 1U << 24U;
}

enum class Bound {
    Exact,
    Lower,
    Upper,
};

struct SearchValue {
    int value = 0;
    Bound bound = Bound::Exact;
};

struct ChildResult {
    SearchValue value;
    std::optional<Action> best_action;
};

auto classify(const int value, const int alpha, const int beta) -> Bound
{
    if (value <= alpha) {
        return Bound::Upper;
    }
    if (value >= beta) {
        return Bound::Lower;
    }
    return Bound::Exact;
}

class SearchContext {
public:
    SearchContext(
        const Partnership target,
        const SearchOptions options,
        const std::size_t buffer_count,
        const std::size_t table_slots)
        : target_(target),
          options_(options),
          table_(table_slots),
          action_buffers_(buffer_count),
          main_variation_(buffer_count),
          killers_(buffer_count)
    {
    }

    auto solve(Position& position) -> SearchResult
    {
        root_depth_ = search_depth(position);
        const int low = score(position, target_);
        if (position.is_finished()) {
            return {low, std::nullopt};
        }
        const int high = low + static_cast<int>(position.tricks_remaining());
        // The root window strictly contains every possible value, so the
        // returned result is exact.
        const ChildResult result =
            search_children(position, low - 1, high + 1, low, high);
        return {result.value.value, result.best_action};
    }

    auto analyze(Position& position) -> std::vector<ActionAnalysis>
    {
        std::vector<ActionAnalysis> results;
        if (position.is_finished()) {
            return results;
        }
        root_depth_ = search_depth(position);
        ActionList& actions = buffer(position);
        position.legal_actions_into(actions);
        results.reserve(actions.size());
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const Action action = actions[index];
            position.apply_known_legal(action);
            const int low = score(position, target_);
            const int high = low + static_cast<int>(position.tricks_remaining());
            // Every displayed action gets its own full natural window so
            // sibling cutoffs can never leak into its value.
            const SearchValue value = options_.use_mtd
                ? mtd_value(position, low, high)
                : evaluate(position, low - 1, high + 1);
            position.undo();
            // The window strictly spans every possible value, so a displayed
            // action can only be backed by an exact result.
            assert(value.bound == Bound::Exact);
            results.push_back({action, value.value});
        }
        return results;
    }

private:
    auto buffer(const Position& position) -> ActionList&
    {
        return action_buffers_[search_depth(position)];
    }

    void store_value(
        const PositionIdentity identity,
        const std::uint32_t depth,
        const std::optional<int> exact,
        const std::optional<int> lower,
        const std::optional<int> upper,
        const std::optional<Action>& best_action)
    {
        if (!options_.use_transposition_table || depth == 0U) {
            return;
        }
        table_.store(identity, depth, exact, lower, upper, best_action);
    }

    // MTD(f): converge on the exact value with repeated null-window searches.
    // Only an exact result is returned; every update uses fail-soft bounds.
    auto mtd_value(Position& position, int lower, int upper) -> SearchValue
    {
        int guess = (lower + upper) / 2;
        const PositionIdentity identity = position.identity();
        if (options_.use_transposition_table) {
            const TranspositionTable::Probe probe = table_.probe(identity);
            if (probe.entry != nullptr && probe.entry->exact.has_value()) {
                const int exact = *probe.entry->exact;
                if (exact >= lower && exact <= upper) {
                    guess = exact;
                }
            }
        }

        for (int iteration = 0; iteration < 64 && lower < upper; ++iteration) {
            const int beta = guess == lower ? guess + 1 : guess;
            if (options_.stats != nullptr) {
                ++options_.stats->pvs_null_windows;
            }
            const SearchValue result = evaluate(position, beta - 1, beta);
            if (result.bound == Bound::Exact) {
                return result;
            }
            if (result.bound == Bound::Lower) {
                lower = std::max(lower, result.value);
            } else {
                upper = std::min(upper, result.value);
            }
            guess = result.value;
        }
        assert(lower == upper);
        return {lower, Bound::Exact};
    }

    auto evaluate(Position& position, int alpha, int beta) -> SearchValue
    {
        if (options_.stats != nullptr) {
            ++options_.stats->nodes;
        }
        const int low = score(position, target_);
        const int high = low + static_cast<int>(position.tricks_remaining());
        if (low == high) {
            return {low, Bound::Exact};
        }

        const std::uint32_t depth = search_depth(position);
        const PositionIdentity identity = position.identity();
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
                    return {
                        *cached->exact,
                        classify(*cached->exact, caller_alpha, caller_beta),
                    };
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
                    if (cached->lower.has_value() && *cached->lower >= beta) {
                        return {*cached->lower, Bound::Lower};
                    }
                    return {*cached->upper, Bound::Upper};
                }
            }
        }

        if (prune && high <= alpha) {
            store_value(identity, depth, std::nullopt, std::nullopt, high, std::nullopt);
            return {high, Bound::Upper};
        }
        if (prune && low >= beta) {
            store_value(identity, depth, std::nullopt, low, std::nullopt, std::nullopt);
            return {low, Bound::Lower};
        }

        const ChildResult result =
            search_children(position, alpha, beta, low, high, cached);
        store_value(
            identity,
            depth,
            result.value.bound == Bound::Exact
                ? std::optional<int>{result.value.value} : std::nullopt,
            result.value.bound == Bound::Lower
                ? std::optional<int>{result.value.value} : std::nullopt,
            result.value.bound == Bound::Upper
                ? std::optional<int>{result.value.value} : std::nullopt,
            result.best_action);
        return result.value;
    }

    auto search_children(
        Position& position,
        const int alpha,
        const int beta,
        const int low,
        const int high,
        const TranspositionEntry* cached = nullptr) -> ChildResult
    {
        const bool prune = options_.use_alpha_beta;
        const bool maximizing = partnership(position.next_player()) == target_;
        int working_alpha = alpha;
        int working_beta = beta;
        int best = maximizing ? low : high;
        std::optional<Action> best_action;

        const std::uint32_t depth = search_depth(position);
        ActionList& actions = buffer(position);
        position.legal_actions_into(actions);
        order_actions(actions, depth, cached);

        bool first = true;
        std::size_t ordered_position = 0;
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const Action action = actions[index];
            position.apply_known_legal(action);

            int value = 0;
            if (!prune || first) {
                value = evaluate(position, working_alpha, working_beta).value;
                first = false;
            } else if (maximizing) {
                if (options_.stats != nullptr) {
                    ++options_.stats->pvs_null_windows;
                }
                value = evaluate(position, working_alpha, working_alpha + 1).value;
                if (value > working_alpha && value < working_beta) {
                    if (options_.stats != nullptr) {
                        ++options_.stats->pvs_researches;
                    }
                    value = evaluate(position, working_alpha, working_beta).value;
                }
            } else {
                if (options_.stats != nullptr) {
                    ++options_.stats->pvs_null_windows;
                }
                value = evaluate(position, working_beta - 1, working_beta).value;
                if (value < working_beta && value > working_alpha) {
                    if (options_.stats != nullptr) {
                        ++options_.stats->pvs_researches;
                    }
                    value = evaluate(position, working_alpha, working_beta).value;
                }
            }
            position.undo();

            // Null-window probes only prove bounds, so a tie is never used to
            // replace the current action here. The root keeps the canonical
            // legal order, where strict improvement already preserves the
            // earliest action among equal values.
            const bool improves = !best_action.has_value() ||
                (maximizing ? value > best : value < best);
            if (improves) {
                best = value;
                best_action = action;
            }
            if (maximizing) {
                working_alpha = std::max(working_alpha, best);
            } else {
                working_beta = std::min(working_beta, best);
            }
            if (prune && working_alpha >= working_beta) {
                if (options_.stats != nullptr) {
                    ++options_.stats->cutoffs;
                    if (ordered_position == 0U) {
                        ++options_.stats->first_move_cutoffs;
                    }
                }
                record_cutoff(action, depth);
                break;
            }
            ++ordered_position;
        }

        if (best_action.has_value()) {
            main_variation_[depth] = *best_action;
        }
        return {{best, classify(best, alpha, beta)}, best_action};
    }

    // Reorders the same legal actions by proven priority only: table best
    // action, then this ply's main-variation action, then the killers. All
    // remaining actions keep the canonical legal order. Static rank ordering
    // and history were measured to increase node counts and are not used.
    void order_actions(
        ActionList& actions,
        const std::uint32_t depth,
        const TranspositionEntry* cached)
    {
        if (depth == root_depth_) {
            return;
        }
        const std::array<std::optional<Action>, 4> priorities = {
            cached != nullptr ? cached->best_action : std::nullopt,
            main_variation_[depth],
            killers_[depth][0],
            killers_[depth][1],
        };
        std::size_t target = 0;
        std::size_t source = 0;
        for (const std::optional<Action>& priority : priorities) {
            if (!priority.has_value() || target >= actions.size()) {
                continue;
            }
            const auto found = std::find(
                actions.begin() + static_cast<std::ptrdiff_t>(target),
                actions.end(),
                *priority);
            if (found == actions.end()) {
                continue;
            }
            std::rotate(
                actions.begin() + static_cast<std::ptrdiff_t>(target),
                found,
                found + 1);
            if (target == 0U) {
                source = &priority - &priorities[0] + 1U;
            }
            ++target;
        }

        if (options_.stats != nullptr) {
            switch (source) {
                case 1:
                    ++options_.stats->ordering_tt;
                    break;
                case 2:
                    ++options_.stats->ordering_mv;
                    break;
                case 3:
                case 4:
                    ++options_.stats->ordering_killer;
                    break;
                default:
                    ++options_.stats->ordering_static;
                    break;
            }
        }
    }

    void record_cutoff(const Action& action, const std::uint32_t depth)
    {
        std::array<std::optional<Action>, 2>& killers = killers_[depth];
        if (action.is_pass()) {
            return;
        }
        if (!killers[0].has_value() || !(*killers[0] == action)) {
            killers[1] = killers[0];
            killers[0] = action;
        }
    }

    std::uint32_t root_depth_ = 0;

    Partnership target_;
    SearchOptions options_;
    TranspositionTable table_;
    std::vector<ActionList> action_buffers_;
    std::vector<std::optional<Action>> main_variation_;
    std::vector<std::array<std::optional<Action>, 2>> killers_;
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
    SearchContext context(
        target,
        options,
        static_cast<std::size_t>(position.tricks_remaining()) * 4U + 1U,
        transposition_slots(position.tricks_remaining()));
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
    const SearchOptions& options)
    -> std::vector<ActionAnalysis>
{
    SearchContext context(
        target,
        options,
        static_cast<std::size_t>(position.tricks_remaining()) * 4U + 1U,
        transposition_slots(position.tricks_remaining()));
    return context.analyze(position);
}

}  // namespace touniu_solver
