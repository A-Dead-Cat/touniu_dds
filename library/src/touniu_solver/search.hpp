#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/position.hpp>
#include <touniu_solver/transposition_table.hpp>

namespace touniu_solver {

enum class Partnership {
    NorthSouth,
    EastWest,
};

struct SearchStats {
    std::uint64_t table_slots = 0;
    std::uint64_t nodes = 0;
    std::uint64_t table_queries = 0;
    std::uint64_t table_hits = 0;
    std::uint64_t collision_rejections = 0;
    std::uint64_t cutoffs = 0;
    std::uint64_t pvs_null_windows = 0;
    std::uint64_t pvs_researches = 0;
    std::uint64_t first_move_cutoffs = 0;
    std::uint64_t ordering_tt = 0;
    std::uint64_t ordering_mv = 0;
    std::uint64_t ordering_killer = 0;
    std::uint64_t ordering_static = 0;
    std::uint64_t equivalent_groups = 0;
    std::uint64_t equivalent_skips = 0;
    std::uint64_t equivalence_checks = 0;
    std::uint64_t equivalence_merged = 0;
    std::uint64_t equivalence_blocked_candidate = 0;
    std::uint64_t equivalence_blocked_plus_two = 0;
    std::uint64_t equivalence_blocked_rank = 0;
    std::uint64_t equivalence_blocked_special = 0;
    std::uint64_t equivalence_blocked_interval = 0;
    std::uint64_t table_replacements = 0;
    std::uint64_t forced_nodes = 0;
    std::uint64_t compact_plain_tail_switches = 0;
};

struct SearchOptions {
    bool use_alpha_beta = true;
    bool use_transposition_table = true;
    SearchStats* stats = nullptr;
    // Per-action analysis converges with MTD(f) null-window probes, which the
    // fixed benchmark preferred over a single PVS window (lower p95). Set to
    // false to use the single-window PVS driver instead.
    bool use_mtd = true;
    // Interior nodes merge plain cards that are indistinguishable for every
    // remaining comparison. The root of analyze_actions() is never merged.
    bool use_equivalent_pruning = true;
    // Wide mode also ignores special or multi-suit cards whose rank lies
    // strictly outside the candidate gap; narrow mode disables a pair as soon
    // as any non-plain card remains. Kept switchable for A/B measurements.
    bool use_wide_equivalence = true;
    // Plain deals key the transposition table on the rank-compressed owner
    // sequence, so positions that only differ by interchangeable cards share
    // an entry. Set to false to compare against the exact CardId identity.
    bool use_shareable_identity = true;
    // Follow a run of single-legal-action positions without searching each
    // link: the value of a forced position is the value of its only child.
    bool use_forced_chain = true;
    // Order the actions that the priority list did not pick by a static score
    // (take the trick, cheapest winner first; when leading, highest card
    // first). Equal scores keep the canonical order.
    bool use_ordering_score = true;
    // A deal that started with special cards may become an ordinary-card tail
    // after a completed trick. Switch that subtree to compact bitmasks;
    // unequal hand sizes and empty-hand PASS remain supported.
    bool use_compact_plain_tail = true;
    // Non-zero overrides the automatic request-local transposition-table
    // capacity. Primarily used for reproducible memory/speed benchmarks.
    std::size_t table_slots_override = 0U;
};

struct SearchResult {
    int tricks = 0;
    std::optional<Action> best_action;
};

struct ActionAnalysis {
    Action action;
    int final_tricks = 0;
};

// Table budget for contract solves, chosen by the caller from the device's
// memory hint. It only changes capacity, never cached values.
enum class ContractTableCapacity {
    Standard,
    LowMemory,
};

// One contract-table query: a starting position plus the partnership whose
// final tricks are requested.
struct ContractQuery {
    Position position;
    Partnership target_partnership = Partnership::NorthSouth;
};

auto solve(Position& position, Partnership target, bool use_alpha_beta = true)
    -> SearchResult;

auto solve(Position& position, Partnership target, const SearchOptions& options)
    -> SearchResult;

// Forces each legal action and solves its continuation with an independent full
// window. All actions in one call share a verified transposition table.
auto analyze_actions(
    Position& position,
    Partnership target,
    bool use_alpha_beta = true) -> std::vector<ActionAnalysis>;

auto analyze_actions(
    Position& position,
    Partnership target,
    const SearchOptions& options) -> std::vector<ActionAnalysis>;

// Solves several contract-table queries for the same deal and strain with one
// shared, verified transposition table. Values are stored canonically from the
// North-South perspective, so queries for different declarers can reuse them.
// Ordering heuristics are reset between queries.
auto solve_contract_batch(
    std::vector<ContractQuery>& queries,
    const SearchOptions& options = {},
    ContractTableCapacity capacity = ContractTableCapacity::Standard)
    -> std::vector<int>;

// Single capacity decision point for contract solves. Exposed for tests; it
// never changes the values produced by the search.
auto contract_table_slots(
    const Position& position,
    ContractTableCapacity capacity) -> std::size_t;

// Pure policy used by the runtime memory detector and by tests. Complex
// eight-card-or-larger analyses use 2^24 slots only when the available memory
// per concurrently active analysis is safely above the 512 MiB table itself.
auto analysis_table_slots_for_memory(
    const Position& position,
    std::size_t available_memory_bytes,
    std::size_t active_analyses) -> std::size_t;

// Conservative equivalence predicate used by the search. Exposed for tests; it
// never reports equivalence when a special, multi-suit or +2 card remains able
// to participate in a later comparison.
auto actions_are_equivalent(
    const Position& position,
    const Action& left,
    const Action& right) -> bool;

}  // namespace touniu_solver
