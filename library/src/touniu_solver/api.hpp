#pragma once

#include <optional>

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

}  // namespace touniu_solver
