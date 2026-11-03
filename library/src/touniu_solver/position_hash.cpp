#include "position_hash.hpp"

namespace touniu_solver {

auto hash_position(const Position& position) -> std::uint64_t
{
    return recompute_position_identity(position).primary;
}

}  // namespace touniu_solver
