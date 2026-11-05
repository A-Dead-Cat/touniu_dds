#ifndef TOUNIU_HEURISTIC_POLICY_HPP
#define TOUNIU_HEURISTIC_POLICY_HPP

#include <touniu_solver/position.hpp>

// Plays the defender's heuristic policy for the seat on move.
//   policy 0: highest-card lead, 1: lowest-card lead, 2: low card of the
//   longest suit, 3: full-information lead, 4: one-trick simulation lead.
// Following always uses the partnership rules (second/third/fourth hand, bomb
// and +2 discipline).
auto heuristic_choose(const touniu_solver::Position& position, int policy)
    -> touniu_solver::Action;

#endif  // TOUNIU_HEURISTIC_POLICY_HPP
