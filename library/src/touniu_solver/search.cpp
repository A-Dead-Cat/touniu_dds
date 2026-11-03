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

using Cache = std::unordered_map<std::uint64_t, int>;

auto evaluate(
    Position& position,
    const Partnership target,
    int alpha,
    int beta,
    const bool prune,
    Cache& cache) -> int
{
    if (position.is_finished()) {
        return score(position, target);
    }
    const std::uint64_t key = hash_position(position);
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }

    const bool maximizing = partnership(position.next_player()) == target;
    int best = maximizing ? std::numeric_limits<int>::min()
                          : std::numeric_limits<int>::max();
    bool cutoff = false;
    for (const Action& action : position.legal_actions()) {
        if (!position.apply(action)) {
            continue;
        }
        const int value = evaluate(position, target, alpha, beta, prune, cache);
        position.undo();
        if (maximizing) {
            best = std::max(best, value);
            alpha = std::max(alpha, best);
        } else {
            best = std::min(best, value);
            beta = std::min(beta, best);
        }
        if (prune && alpha >= beta) {
            cutoff = true;
            break;
        }
    }
    if (!cutoff) {
        cache.emplace(key, best);
    }
    return best;
}

}  // namespace

auto solve(Position& position, const Partnership target, const bool use_alpha_beta)
    -> SearchResult
{
    if (position.is_finished()) {
        return {score(position, target), std::nullopt};
    }

    Cache cache;
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
            position, target, alpha, beta, use_alpha_beta, cache);
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
