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

}  // namespace touniu_solver
