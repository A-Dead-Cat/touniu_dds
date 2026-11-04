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

// Canonical value: the final number of tricks won by North-South. East-West
// queries convert with (total - canonical), which lets one table serve every
// declarer of the same deal and strain.
auto score_ns(const Position& position) -> int
{
    return static_cast<int>(
        position.tricks_won(Seat::North) + position.tricks_won(Seat::South));
}

auto total_tricks(const Position& position) -> int
{
    int won = 0;
    for (int seat = 0; seat < 4; ++seat) {
        won += static_cast<int>(position.tricks_won(static_cast<Seat>(seat)));
    }
    return won + static_cast<int>(position.tricks_remaining());
}

auto to_target(
    const int canonical_ns,
    const int total,
    const Partnership target) -> int
{
    return target == Partnership::NorthSouth ? canonical_ns : total - canonical_ns;
}

auto search_depth(const Position& position) -> std::uint32_t
{
    return position.tricks_remaining() * 4U -
           static_cast<std::uint32_t>(position.current_trick().size());
}

// Contract solves explore far fewer nodes than per-action analysis, so their
// table is sized by the cards in play and capped by the caller's memory tier:
// 128 MiB per worker on standard devices, 32 MiB when memory is unknown or
// small. The Playground's fixed 13-trick limit must not shrink the table below
// what the dealt cards call for.
auto transposition_slots(
    const Position& position,
    const ContractTableCapacity capacity) -> std::size_t
{
    std::size_t cards = position.current_trick().size();
    for (int seat = 0; seat < 4; ++seat) {
        cards += position.hand(static_cast<Seat>(seat)).size();
    }
    const std::size_t per_seat = (cards + 3U) / 4U;
    if (per_seat <= 5U) {
        return 1U << 15U;
    }
    if (per_seat == 6U) {
        return 1U << 17U;
    }
    if (per_seat == 7U) {
        return 1U << 20U;
    }
    return capacity == ContractTableCapacity::LowMemory ? 1U << 20U : 1U << 22U;
}

// Per-action analysis explores a much larger tree than a single contract
// solve, so its table is sized by the cards actually in play (not by the trick
// limit, which can exceed the dealt cards in the Playground) and grows to a
// 2^25-slot cap for ten or more cards per seat.
auto analysis_transposition_slots(const Position& position) -> std::size_t
{
    std::size_t cards = position.current_trick().size();
    for (int seat = 0; seat < 4; ++seat) {
        cards += position.hand(static_cast<Seat>(seat)).size();
    }
    const std::size_t per_seat = (cards + 3U) / 4U;
    if (per_seat <= 5U) {
        return 1U << 15U;
    }
    if (per_seat == 6U) {
        return 1U << 17U;
    }
    if (per_seat == 7U) {
        return 1U << 20U;
    }
    if (per_seat == 8U) {
        return 1U << 23U;
    }
    if (per_seat == 9U) {
        return 1U << 23U;
    }
    if (per_seat == 10U) {
        return 1U << 22U;
    }
    if (per_seat <= 12U) {
        return 1U << 24U;
    }
    return 1U << 25U;
}

auto is_plain_card(const Card& card) -> bool
{
    return (card.kind() == CardKind::Normal || card.kind() == CardKind::Floating) &&
           card.intrinsic_suits().size() == 1U;
}

auto rank_between(const Card& card, const Card& left, const Card& right) -> bool
{
    return card.rank() > left.rank() && card.rank() < right.rank();
}

enum class EquivalenceBlock {
    None,
    Candidate,
    PlusTwo,
    Rank,
    Special,
    NonPlain,
    Interval,
};

// Decides whether `right` can reuse the value already computed for `left`.
// In wide mode a special or multi-suit universe card only blocks the pair when
// its rank lies inside [low, high] (inclusive), because a rank strictly outside
// the gap compares identically against both candidates whatever suit it is
// played as. A +2 held in a hand always blocks: its future draws could land
// between the candidates. A +2 already in the current trick has finished
// drawing, so only its rank matters.
auto equivalence_block(
    const Position& position,
    const Action& left,
    const Action& right,
    const bool wide) -> EquivalenceBlock
{
    if (left.is_pass() || right.is_pass()) {
        return EquivalenceBlock::Candidate;
    }
    if (left.mode() != right.mode() ||
        left.effective_suit() != right.effective_suit()) {
        return EquivalenceBlock::Candidate;
    }
    const Card* left_card = position.card(*left.card_id());
    const Card* right_card = position.card(*right.card_id());
    if (left_card == nullptr || right_card == nullptr) {
        return EquivalenceBlock::Candidate;
    }
    if (!is_plain_card(*left_card) || !is_plain_card(*right_card) ||
        left_card->intrinsic_suits() != right_card->intrinsic_suits()) {
        return EquivalenceBlock::Candidate;
    }

    if (wide && position.has_equivalence_universe()) {
        return position.equivalent_actions(left, right)
            ? EquivalenceBlock::None : EquivalenceBlock::Interval;
    }

    const Card& low = left_card->rank() < right_card->rank()
        ? *left_card : *right_card;
    const Card& high = left_card->rank() < right_card->rank()
        ? *right_card : *left_card;

    const auto classify = [&](const Card& card, const bool in_hand) {
        if (card.kind() == CardKind::PlusTwo && in_hand) {
            return EquivalenceBlock::PlusTwo;
        }
        if (is_plain_card(card)) {
            return rank_between(card, low, high)
                ? EquivalenceBlock::Rank : EquivalenceBlock::None;
        }
        if (!wide) {
            return EquivalenceBlock::NonPlain;
        }
        const bool inside = card.rank() >= low.rank() && card.rank() <= high.rank();
        return inside ? EquivalenceBlock::Special : EquivalenceBlock::None;
    };

    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            if (id == left_card->id() || id == right_card->id()) {
                continue;
            }
            const Card* card = position.card(id);
            if (card == nullptr) {
                return EquivalenceBlock::Candidate;
            }
            const EquivalenceBlock block = classify(*card, true);
            if (block != EquivalenceBlock::None) {
                return block;
            }
        }
    }
    for (const TrickPlay& play : position.current_trick()) {
        if (play.action.is_pass()) {
            continue;
        }
        const Card* card = position.card(*play.action.card_id());
        if (card == nullptr) {
            return EquivalenceBlock::Candidate;
        }
        const EquivalenceBlock block = classify(*card, false);
        if (block != EquivalenceBlock::None) {
            return block;
        }
    }
    return EquivalenceBlock::None;
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
        const SearchOptions options,
        const std::size_t buffer_count,
        const std::size_t table_slots)
        : options_(options),
          table_(table_slots),
          action_buffers_(buffer_count),
          merge_buffers_(buffer_count),
          main_variation_(buffer_count),
          killers_(buffer_count)
    {
    }

    // Canonical North-South result plus the action that achieves it.
    auto solve(Position& position) -> SearchResult
    {
        root_depth_ = search_depth(position);
        const int low = score_ns(position);
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

    auto solve_for(Position& position, const Partnership target) -> SearchResult
    {
        const int total = total_tricks(position);
        SearchResult result = solve(position);
        result.tricks = to_target(result.tricks, total, target);
        return result;
    }

    auto analyze(Position& position, const Partnership target)
        -> std::vector<ActionAnalysis>
    {
        std::vector<ActionAnalysis> results;
        if (position.is_finished()) {
            return results;
        }
        root_depth_ = search_depth(position);
        const int total = total_tricks(position);
        ActionList& actions = buffer(position);
        position.legal_actions_into(actions);
        results.reserve(actions.size());
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const Action action = actions[index];
            position.apply_known_legal(action);
            const int low = score_ns(position);
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
            results.push_back({action, to_target(value.value, total, target)});
        }
        return results;
    }

    void reset_heuristics()
    {
        for (auto& action : main_variation_) {
            action.reset();
        }
        for (auto& killers : killers_) {
            killers[0].reset();
            killers[1].reset();
        }
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
        if (table_.store(identity, depth, exact, lower, upper, best_action) &&
            options_.stats != nullptr) {
            ++options_.stats->table_replacements;
        }
    }

    // MTD(f): converge on the exact value with repeated null-window searches.
    // Only an exact result is returned; every update uses fail-soft bounds.
    auto mtd_value(Position& position, int lower, int upper) -> SearchValue
    {
        int guess = (lower + upper) / 2;
        const PositionIdentity identity = options_.use_shareable_identity
            ? position.table_identity() : position.identity();
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

    // A position with exactly one legal action has nothing to choose, so the
    // search can follow the forced chain without a table probe or a window
    // check per link. The chain stops as soon as a position branches, ends, or
    // hits the safety cap, and every applied action is undone in reverse.
    auto apply_forced_chain(Position& position) -> std::uint32_t
    {
        std::uint32_t applied = 0;
        while (applied < 64U && !position.is_finished()) {
            ActionList& actions = buffer(position);
            position.legal_actions_into(actions);
            if (actions.size() != 1U) {
                break;
            }
            position.apply_known_legal(actions.front());
            ++applied;
        }
        return applied;
    }

    void undo_forced_chain(Position& position, std::uint32_t applied)
    {
        while (applied > 0U) {
            position.undo();
            --applied;
        }
    }

    auto evaluate(Position& position, int alpha, int beta) -> SearchValue
    {
        if (options_.stats != nullptr) {
            ++options_.stats->nodes;
        }
        if (options_.use_forced_chain) {
            const std::uint32_t chain = apply_forced_chain(position);
            if (chain > 0U) {
                if (options_.stats != nullptr) {
                    options_.stats->forced_nodes += chain;
                }
                const SearchValue chained = evaluate(position, alpha, beta);
                undo_forced_chain(position, chain);
                return chained;
            }
        }
        int low = score_ns(position);
        int high = low + static_cast<int>(position.tricks_remaining());
        if (low != high) {
            // Sure-trick lower bound: at a no-trump lead with no live trump-like
            // card, every suit whose highest live card is in the leader's hand
            // is a guaranteed trick for the leader's partnership.
            const Position::SureTricks sure = position.leader_sure_tricks();
            if (sure.tricks > 0U) {
                const int count = static_cast<int>(sure.tricks);
                if (sure.north_south) {
                    low += count;
                } else {
                    const int total = total_tricks(position);
                    const int east_west = static_cast<int>(
                        position.tricks_won(Seat::West) +
                        position.tricks_won(Seat::East));
                    high = std::min(high, total - (east_west + count));
                }
            }
        }
        if (low == high) {
            return {low, Bound::Exact};
        }

        const std::uint32_t depth = search_depth(position);
        const PositionIdentity identity = options_.use_shareable_identity
            ? position.table_identity() : position.identity();
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

        if (options_.use_forced_chain) {
            const std::uint32_t chain = apply_forced_chain(position);
            if (chain > 0U) {
                if (options_.stats != nullptr) {
                    options_.stats->forced_nodes += chain;
                }
                const SearchValue chained = evaluate(position, alpha, beta);
                undo_forced_chain(position, chain);
                return chained;
            }
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
        const bool maximizing =
            partnership(position.next_player()) == Partnership::NorthSouth;
        int working_alpha = alpha;
        int working_beta = beta;
        int best = maximizing ? low : high;
        std::optional<Action> best_action;

        const std::uint32_t depth = search_depth(position);
        ActionList& actions = buffer(position);
        position.legal_actions_into(actions);
        order_actions(position, actions, depth, cached);

        ActionList& representatives = merge_buffers_[depth];
        representatives.clear();
        bool merged_any = false;

        bool first = true;
        std::size_t ordered_position = 0;
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const Action action = actions[index];
            if (options_.use_equivalent_pruning) {
                bool merged = false;
                for (const Action& representative : representatives) {
                    const EquivalenceBlock block = equivalence_block(
                        position,
                        representative,
                        action,
                        options_.use_wide_equivalence);
                    if (options_.stats != nullptr) {
                        ++options_.stats->equivalence_checks;
                    }
                    if (block == EquivalenceBlock::None) {
                        merged = true;
                        if (options_.stats != nullptr) {
                            ++options_.stats->equivalence_merged;
                        }
                        break;
                    }
                    if (options_.stats != nullptr) {
                        switch (block) {
                            case EquivalenceBlock::Candidate:
                                ++options_.stats->equivalence_blocked_candidate;
                                break;
                            case EquivalenceBlock::PlusTwo:
                                ++options_.stats->equivalence_blocked_plus_two;
                                break;
                            case EquivalenceBlock::Rank:
                                ++options_.stats->equivalence_blocked_rank;
                                break;
                            case EquivalenceBlock::Special:
                            case EquivalenceBlock::NonPlain:
                                ++options_.stats->equivalence_blocked_special;
                                break;
                            case EquivalenceBlock::Interval:
                                ++options_.stats->equivalence_blocked_interval;
                                break;
                            case EquivalenceBlock::None:
                                break;
                        }
                    }
                }
                if (merged) {
                    if (options_.stats != nullptr) {
                        ++options_.stats->equivalent_skips;
                    }
                    merged_any = true;
                    continue;
                }
                representatives.push_back(action);
            }
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

        if (merged_any && options_.stats != nullptr) {
            ++options_.stats->equivalent_groups;
        }
        if (best_action.has_value()) {
            main_variation_[depth] = *best_action;
        }
        return {{best, classify(best, alpha, beta)}, best_action};
    }

    // Static ordering score: take the trick if the card can, cheapest winner
    // first, otherwise throw the lowest card; when leading, try the highest
    // card first. Only the order changes, never the action set or the value,
    // and equal scores keep the canonical order through a stable sort.
    auto static_score(const Position& position, const Action& action) const -> int
    {
        if (action.is_pass()) {
            return -1000000;
        }
        const Card* card = position.card(*action.card_id());
        if (card == nullptr) {
            return 0;
        }
        const int rank_thousandths = card->rank().millionths() / 1000;
        const Trick& trick = position.current_trick();
        if (trick.empty()) {
            return rank_thousandths;
        }
        const bool no_trump = position.contract() == Contract::NoTrump;
        const std::size_t trump =
            static_cast<std::size_t>(static_cast<int>(position.contract()));
        std::size_t led = 4U;
        int best_class = -1;
        int best_rank = -1;
        for (const TrickPlay& play : trick) {
            if (play.action.is_pass()) {
                continue;
            }
            const Card* played = position.card(*play.action.card_id());
            if (played == nullptr) {
                continue;
            }
            if (!play.action.effective_suit().has_value()) {
                continue;
            }
            const std::size_t suit =
                static_cast<std::size_t>(*play.action.effective_suit());
            if (led == 4U) {
                led = suit;
            }
            const int card_class = (!no_trump && suit == trump)
                ? 3 : (suit == led ? 2 : 0);
            const int card_rank = played->rank().millionths();
            if (card_class > best_class ||
                (card_class == best_class && card_rank > best_rank)) {
                best_class = card_class;
                best_rank = card_rank;
            }
        }
        if (!action.effective_suit().has_value()) {
            return 0;
        }
        const std::size_t suit =
            static_cast<std::size_t>(*action.effective_suit());
        const int card_class = (!no_trump && suit == trump)
            ? 3 : (suit == led ? 2 : 0);
        const bool wins = card_class > best_class ||
            (card_class == best_class && rank_thousandths * 1000 > best_rank);
        if (wins) {
            return 1000000 - rank_thousandths;
        }
        return -rank_thousandths;
    }

    // Reorders the same legal actions by proven priority only: table best
    // action, then this ply's main-variation action, then the killers. All
    // remaining actions keep the canonical legal order. Static rank ordering
    // and history were measured to increase node counts and are not used.
    void order_actions(
        Position& position,
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
        if (options_.use_ordering_score && position.has_shareable_identity() &&
            actions.size() > target + 1U &&
            actions.size() <= ActionList::InlineCapacity) {
            // Score once, then a stable insertion sort so equal scores keep the
            // canonical order.
            std::array<int, ActionList::InlineCapacity> scores{};
            for (std::size_t index = target; index < actions.size(); ++index) {
                scores[index] = static_score(position, actions[index]);
            }
            for (std::size_t index = target + 1U; index < actions.size(); ++index) {
                const Action action = actions[index];
                const int score = scores[index];
                std::size_t place = index;
                while (place > target && scores[place - 1U] < score) {
                    actions[place] = actions[place - 1U];
                    scores[place] = scores[place - 1U];
                    --place;
                }
                actions[place] = action;
                scores[place] = score;
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

    SearchOptions options_;
    TranspositionTable table_;
    std::vector<ActionList> action_buffers_;
    std::vector<ActionList> merge_buffers_;
    std::vector<std::optional<Action>> main_variation_;
    std::vector<std::array<std::optional<Action>, 2>> killers_;
};

}  // namespace

auto actions_are_equivalent(
    const Position& position,
    const Action& left,
    const Action& right) -> bool
{
    return equivalence_block(position, left, right, true) ==
           EquivalenceBlock::None;
}


auto solve(Position& position, Partnership target, bool use_alpha_beta)
    -> SearchResult
{
    return solve(position, target, SearchOptions{use_alpha_beta, true, nullptr});
}

auto solve(Position& position, Partnership target, const SearchOptions& options)
    -> SearchResult
{
    SearchContext context(
        options,
        static_cast<std::size_t>(position.tricks_remaining()) * 4U + 1U,
        transposition_slots(position, ContractTableCapacity::Standard));
    return context.solve_for(position, target);
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
        options,
        static_cast<std::size_t>(position.tricks_remaining()) * 4U + 1U,
        analysis_transposition_slots(position));
    return context.analyze(position, target);
}

auto contract_table_slots(
    const Position& position,
    const ContractTableCapacity capacity) -> std::size_t
{
    return transposition_slots(position, capacity);
}

auto solve_contract_batch(
    std::vector<ContractQuery>& queries,
    const SearchOptions& options,
    const ContractTableCapacity capacity) -> std::vector<int>
{
    std::vector<int> results;
    results.reserve(queries.size());
    if (queries.empty()) {
        return results;
    }

    std::size_t buffer_count = 1U;
    std::size_t table_slots = 1U << 15U;
    for (const ContractQuery& query : queries) {
        const std::uint32_t tricks = query.position.tricks_remaining();
        buffer_count = std::max(
            buffer_count, static_cast<std::size_t>(tricks) * 4U + 1U);
        table_slots = std::max(
            table_slots, transposition_slots(query.position, capacity));
    }

    // One shared table for every declarer of this deal and strain. Heuristics
    // are per-query so one declarer never biases another's move order.
    SearchContext context(options, buffer_count, table_slots);
    for (ContractQuery& query : queries) {
        context.reset_heuristics();
        results.push_back(
            context.solve_for(query.position, query.target_partnership).tricks);
    }
    return results;
}

}  // namespace touniu_solver
