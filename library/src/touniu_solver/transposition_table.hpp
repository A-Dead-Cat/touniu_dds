#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <touniu_solver/legal_moves.hpp>
#include <touniu_solver/position_hash.hpp>

namespace touniu_solver {

struct TranspositionEntry {
    PositionIdentity identity{};
    std::uint32_t depth = 0;
    std::optional<int> exact;
    std::optional<int> lower;
    std::optional<int> upper;
    std::optional<Action> best_action;
};

// Request-scoped table keyed by the primary identity and verified by the
// secondary identity. Entries are depth-scoped and merge bounds monotonically:
// lower bounds only strengthen, upper bounds only weaken downward, and an exact
// value is never replaced by a weaker record.
class TranspositionTable {
public:
    struct Probe {
        const TranspositionEntry* entry = nullptr;
        bool collision = false;
    };

    [[nodiscard]] auto probe(const PositionIdentity identity) -> Probe
    {
        const auto bucket = buckets_.find(identity.primary);
        if (bucket == buckets_.end()) {
            return {};
        }
        bool saw_primary = false;
        for (const TranspositionEntry& entry : bucket->second) {
            saw_primary = true;
            if (entry.identity == identity) {
                return {&entry, false};
            }
        }
        return {nullptr, saw_primary};
    }

    void store(
        const PositionIdentity identity,
        const std::uint32_t depth,
        const std::optional<int> exact,
        const std::optional<int> lower,
        const std::optional<int> upper,
        const std::optional<Action>& best_action)
    {
        std::vector<TranspositionEntry>& bucket = buckets_[identity.primary];
        auto found = std::find_if(
            bucket.begin(),
            bucket.end(),
            [identity](const TranspositionEntry& entry) {
                return entry.identity == identity;
            });

        if (found == bucket.end()) {
            bucket.push_back(TranspositionEntry{identity, depth, exact, lower, upper, best_action});
            return;
        }

        if (depth < found->depth) {
            return;
        }
        if (depth > found->depth) {
            // A deeper result supersedes any shallower incompatible record.
            found->depth = depth;
            found->exact = exact;
            found->lower = lower;
            found->upper = upper;
            found->best_action = best_action;
            return;
        }

        if (found->exact.has_value()) {
            return;
        }
        if (exact.has_value()) {
            found->exact = exact;
            found->lower.reset();
            found->upper.reset();
            if (best_action.has_value()) {
                found->best_action = best_action;
            }
            return;
        }
        if (lower.has_value()) {
            found->lower = found->lower.has_value()
                ? std::max(*found->lower, *lower) : lower;
        }
        if (upper.has_value()) {
            found->upper = found->upper.has_value()
                ? std::min(*found->upper, *upper) : upper;
        }
        if (!found->best_action.has_value() && best_action.has_value()) {
            found->best_action = best_action;
        }
        if (found->lower.has_value() && found->upper.has_value() &&
            *found->lower == *found->upper) {
            found->exact = *found->lower;
            found->lower.reset();
            found->upper.reset();
        }
    }

    [[nodiscard]] auto size() const -> std::size_t
    {
        std::size_t total = 0;
        for (const auto& bucket : buckets_) {
            total += bucket.second.size();
        }
        return total;
    }

private:
    std::unordered_map<std::uint64_t, std::vector<TranspositionEntry>> buckets_;
};

}  // namespace touniu_solver
