#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>
#include <touniu_solver/position_hash.hpp>

namespace touniu_solver {

// Logical view of a table record. `probe()` fills a scratch instance owned by
// the table, so the returned pointer is only valid until the next probe/store.
struct TranspositionEntry {
    PositionIdentity identity{};
    std::uint32_t depth = 0;
    std::optional<int> exact;
    std::optional<int> lower;
    std::optional<int> upper;
    std::optional<Action> best_action;
};

// Request-scoped, bounded table keyed by the primary identity and verified by
// the secondary identity. Slots are stored in a flat, fixed-size array so the
// table has a hard memory bound even for deep searches. Entries within a bucket
// are merged monotonically; a full bucket replaces its shallowest record, which
// can only cost cache hits, never exactness.
class TranspositionTable {
public:
    static constexpr std::size_t BucketSize = 4;

    struct Probe {
        const TranspositionEntry* entry = nullptr;
        bool collision = false;
    };

    // `slot_count` is rounded up to a whole number of buckets. Zero selects a
    // small default suitable for tests.
    explicit TranspositionTable(std::size_t slot_count = 1U << 10U);

    [[nodiscard]] auto probe(PositionIdentity identity) -> Probe;

    void store(
        PositionIdentity identity,
        std::uint32_t depth,
        std::optional<int> exact,
        std::optional<int> lower,
        std::optional<int> upper,
        const std::optional<Action>& best_action);

    [[nodiscard]] auto size() const -> std::size_t
    {
        return live_entries_;
    }

    [[nodiscard]] auto slot_count() const -> std::size_t
    {
        return slots_.size();
    }

private:
    struct Slot {
        std::uint64_t primary = 0;
        std::uint64_t verifier = 0;
        std::uint64_t card_id = 0;
        std::uint8_t depth = 0;
        std::uint8_t flags = 0;
        std::int8_t exact = 0;
        std::int8_t lower = 0;
        std::int8_t upper = 0;
        std::uint8_t mode = 0;
        std::uint8_t suit = 0;
        std::uint8_t pass = 0;
    };

    static_assert(sizeof(Slot) == 32, "transposition slot must stay compact");

    static constexpr std::uint8_t HasExact = 0x01;
    static constexpr std::uint8_t HasLower = 0x02;
    static constexpr std::uint8_t HasUpper = 0x04;
    static constexpr std::uint8_t HasCardAction = 0x08;
    static constexpr std::uint8_t HasPassAction = 0x10;
    static constexpr std::uint8_t NoMode = 0xFF;
    static constexpr std::uint8_t NoSuit = 0xFF;

    void load(const Slot& slot);
    void merge(Slot& slot, std::uint32_t depth, std::optional<int> exact,
               std::optional<int> lower, std::optional<int> upper,
               const std::optional<Action>& best_action);
    static void write_best_action(Slot& slot, const std::optional<Action>& action);

    std::vector<Slot> slots_;
    std::size_t bucket_mask_ = 0;
    std::size_t live_entries_ = 0;
    TranspositionEntry scratch_;
};

}  // namespace touniu_solver
