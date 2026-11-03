#pragma once

#include <optional>
#include <vector>

#include <touniu_solver/search.hpp>

namespace touniu_solver {

enum class SolveError {
    None,
    InvalidScenario,
};

struct SolveRequest {
    Scenario scenario;
    Partnership target = Partnership::NorthSouth;
    bool use_alpha_beta = true;
};

struct SolveResponse {
    SolveError error = SolveError::None;
    std::optional<SearchResult> result;
};

// Validates and solves a copy of the supplied scenario. The caller's request
// remains unchanged even though the search applies and undoes moves internally.
[[nodiscard]] auto solve_scenario(const SolveRequest& request) -> SolveResponse;

struct ActionAnalysis {
    Action action;
    int final_tricks = 0;
};

// Forces each legal action in turn and solves only its continuation optimally.
// `final_tricks` is the target partnership's accumulated wins, plus a trick
// awarded by the forced action when it completes one, plus the optimal
// continuation. The position is left exactly as it was found.
[[nodiscard]] auto analyze_actions(
    Position& position,
    Partnership target,
    bool use_alpha_beta = true) -> std::vector<ActionAnalysis>;

}  // namespace touniu_solver
