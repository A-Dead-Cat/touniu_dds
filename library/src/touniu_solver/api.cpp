#include <touniu_solver/api.hpp>

namespace touniu_solver {

auto solve_scenario(const SolveRequest& request) -> SolveResponse
{
    auto position = Position::create(request.scenario);
    if (!position.has_value()) {
        return {SolveError::InvalidScenario, std::nullopt};
    }

    return {
        SolveError::None,
        solve(*position, request.target, request.use_alpha_beta),
    };
}

auto analyze_actions(
    Position& position,
    const Partnership target,
    const bool use_alpha_beta) -> std::vector<ActionAnalysis>
{
    std::vector<ActionAnalysis> results;
    results.reserve(position.legal_actions().size());
    for (const Action& action : position.legal_actions()) {
        if (!position.apply(action)) {
            continue;
        }
        const SearchResult continuation = solve(position, target, use_alpha_beta);
        position.undo();
        results.push_back({action, continuation.tricks});
    }
    return results;
}

}  // namespace touniu_solver
