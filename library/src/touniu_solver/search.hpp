#pragma once

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
    std::uint64_t nodes = 0;
    std::uint64_t table_queries = 0;
    std::uint64_t table_hits = 0;
    std::uint64_t collision_rejections = 0;
    std::uint64_t cutoffs = 0;
    std::uint64_t pvs_null_windows = 0;
    std::uint64_t pvs_researches = 0;
};

struct SearchOptions {
    bool use_alpha_beta = true;
    bool use_transposition_table = true;
    SearchStats* stats = nullptr;
    // Per-action analysis converges with MTD(f) null-window probes, which the
    // fixed benchmark preferred over a single PVS window (lower p95). Set to
    // false to use the single-window PVS driver instead.
    bool use_mtd = true;
};

struct SearchResult {
    int tricks = 0;
    std::optional<Action> best_action;
};

struct ActionAnalysis {
    Action action;
    int final_tricks = 0;
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

}  // namespace touniu_solver
