#include "search.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

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

enum class Bound {
    Exact,
    Lower,
    Upper,
};

struct TableEntry {
    int value;
    std::uint32_t depth;
    Bound bound;
    std::optional<Action> best_action;
};

using Table = std::unordered_map<std::uint64_t, TableEntry>;

auto search_depth(const Position& position) -> std::uint32_t
{
    return position.tricks_remaining() * 4U -
           static_cast<std::uint32_t>(position.current_trick().size());
}

auto evaluate(
    Position& position,
    const Partnership target,
    int alpha,
    int beta,
    const bool prune,
    Table& table) -> int
{
    if (position.is_finished()) {
        return score(position, target);
    }

    const std::uint64_t key = hash_position(position);
    const std::uint32_t depth = search_depth(position);
    std::optional<Action> table_action;
    if (const auto found = table.find(key);
        found != table.end() && found->second.depth >= depth) {
        const TableEntry& entry = found->second;
        table_action = entry.best_action;
        if (entry.bound == Bound::Exact) {
            return entry.value;
        }
        if (prune && entry.bound == Bound::Lower) {
            alpha = std::max(alpha, entry.value);
        } else if (prune && entry.bound == Bound::Upper) {
            beta = std::min(beta, entry.value);
        }
        if (prune && alpha >= beta) {
            return entry.value;
        }
    }

    const int original_alpha = alpha;
    const int original_beta = beta;
    const bool maximizing = partnership(position.next_player()) == target;
    int best = maximizing ? std::numeric_limits<int>::min()
                          : std::numeric_limits<int>::max();
    std::optional<Action> best_action;
    std::vector<Action> actions = position.legal_actions();
    if (table_action.has_value()) {
        const auto preferred = std::find(actions.begin(), actions.end(), *table_action);
        if (preferred != actions.end()) {
            std::iter_swap(actions.begin(), preferred);
        }
    }
    for (const Action& action : actions) {
        if (!position.apply(action)) {
            continue;
        }
        const int value = evaluate(position, target, alpha, beta, prune, table);
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
            break;
        }
    }

    Bound bound = Bound::Exact;
    if (prune && best <= original_alpha) {
        bound = Bound::Upper;
    } else if (prune && best >= original_beta) {
        bound = Bound::Lower;
    }
    table.insert_or_assign(key, TableEntry{best, depth, bound, best_action});
    return best;
}

}  // namespace

auto solve(Position& position, const Partnership target, const bool use_alpha_beta)
    -> SearchResult
{
    if (position.is_finished()) {
        return {score(position, target), std::nullopt};
    }

    Table table;
    const bool maximizing = partnership(position.next_player()) == target;
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
        const int value = evaluate(
            position, target, alpha, beta, use_alpha_beta, table);
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

}  // namespace touniu_solver
