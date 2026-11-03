#include "transposition_table.hpp"

#include <algorithm>

namespace touniu_solver {

TranspositionTable::TranspositionTable(const std::size_t slot_count)
{
    std::size_t slots = std::max<std::size_t>(slot_count, BucketSize);
    // Round up so the bucket count is a power of two.
    std::size_t buckets = 1;
    while (buckets * BucketSize < slots) {
        buckets <<= 1U;
    }
    slots_.resize(buckets * BucketSize);
    bucket_mask_ = buckets - 1U;
}

namespace {

auto encode_bound(const std::optional<int> value) -> std::int8_t
{
    return value.has_value() ? static_cast<std::int8_t>(*value) : 0;
}

}  // namespace

void TranspositionTable::load(const Slot& slot)
{
    scratch_.identity = PositionIdentity{slot.primary, slot.verifier};
    scratch_.depth = slot.depth;
    scratch_.exact = (slot.flags & HasExact) != 0U
        ? std::optional<int>{slot.exact} : std::nullopt;
    scratch_.lower = (slot.flags & HasLower) != 0U
        ? std::optional<int>{slot.lower} : std::nullopt;
    scratch_.upper = (slot.flags & HasUpper) != 0U
        ? std::optional<int>{slot.upper} : std::nullopt;
    if ((slot.flags & HasPassAction) != 0U) {
        scratch_.best_action = Action::pass();
    } else if ((slot.flags & HasCardAction) != 0U) {
        scratch_.best_action = Action::play(
            CardId{slot.card_id},
            static_cast<PlayMode>(slot.mode),
            static_cast<Suit>(slot.suit));
    } else {
        scratch_.best_action.reset();
    }
}

auto TranspositionTable::probe(const PositionIdentity identity) -> Probe
{
    const std::size_t base = (identity.primary & bucket_mask_) * BucketSize;
    bool saw_primary = false;
    for (std::size_t index = 0; index < BucketSize; ++index) {
        const Slot& slot = slots_[base + index];
        if (slot.depth == 0U) {
            continue;
        }
        if (slot.primary == identity.primary && slot.verifier == identity.verifier) {
            load(slot);
            return {&scratch_, false};
        }
        if (slot.primary == identity.primary) {
            saw_primary = true;
        }
    }
    return {nullptr, saw_primary};
}

void TranspositionTable::write_best_action(
    Slot& slot,
    const std::optional<Action>& action)
{
    slot.flags &= static_cast<std::uint8_t>(~(HasCardAction | HasPassAction));
    if (!action.has_value()) {
        return;
    }
    if (action->is_pass()) {
        slot.flags |= HasPassAction;
        return;
    }
    slot.flags |= HasCardAction;
    slot.card_id = action->card_id()->value;
    slot.mode = static_cast<std::uint8_t>(*action->mode());
    slot.suit = static_cast<std::uint8_t>(*action->effective_suit());
}

void TranspositionTable::merge(
    Slot& slot,
    const std::uint32_t depth,
    const std::optional<int> exact,
    const std::optional<int> lower,
    const std::optional<int> upper,
    const std::optional<Action>& best_action)
{
    if (depth < slot.depth) {
        return;
    }
    if (depth > slot.depth) {
        // A deeper result supersedes any shallower incompatible record.
        slot.depth = static_cast<std::uint8_t>(depth);
        slot.flags = 0U;
        write_best_action(slot, best_action);
    } else if ((slot.flags & HasExact) == 0U && exact.has_value()) {
        slot.exact = encode_bound(exact);
        slot.flags |= HasExact;
        slot.flags &= static_cast<std::uint8_t>(~(HasLower | HasUpper));
        if (best_action.has_value()) {
            write_best_action(slot, best_action);
        }
    }

    if ((slot.flags & HasExact) != 0U) {
        return;
    }
    if (lower.has_value()) {
        if ((slot.flags & HasLower) == 0U || *lower > slot.lower) {
            slot.lower = encode_bound(lower);
            slot.flags |= HasLower;
        }
    }
    if (upper.has_value()) {
        if ((slot.flags & HasUpper) == 0U || *upper < slot.upper) {
            slot.upper = encode_bound(upper);
            slot.flags |= HasUpper;
        }
    }
    if (best_action.has_value() &&
        (slot.flags & (HasCardAction | HasPassAction)) == 0U) {
        write_best_action(slot, best_action);
    }
    if ((slot.flags & HasLower) != 0U && (slot.flags & HasUpper) != 0U &&
        slot.lower >= slot.upper) {
        slot.exact = slot.lower;
        slot.flags |= HasExact;
        slot.flags &= static_cast<std::uint8_t>(~(HasLower | HasUpper));
    }
}

void TranspositionTable::store(
    const PositionIdentity identity,
    const std::uint32_t depth,
    const std::optional<int> exact,
    const std::optional<int> lower,
    const std::optional<int> upper,
    const std::optional<Action>& best_action)
{
    const std::size_t base = (identity.primary & bucket_mask_) * BucketSize;
    Slot* victim = nullptr;
    for (std::size_t index = 0; index < BucketSize; ++index) {
        Slot& slot = slots_[base + index];
        if (slot.depth == 0U) {
            slot.primary = identity.primary;
            slot.verifier = identity.verifier;
            slot.depth = static_cast<std::uint8_t>(depth);
            slot.flags = 0U;
            write_best_action(slot, best_action);
            merge(slot, depth, exact, lower, upper, best_action);
            ++live_entries_;
            return;
        }
        if (slot.primary == identity.primary && slot.verifier == identity.verifier) {
            merge(slot, depth, exact, lower, upper, best_action);
            return;
        }
        if (victim == nullptr || slot.depth < victim->depth) {
            victim = &slot;
        }
    }

    // Bucket is full of other positions; replace the shallowest record only
    // when the new result is at least as deep.
    if (victim != nullptr && depth >= victim->depth) {
        victim->primary = identity.primary;
        victim->verifier = identity.verifier;
        victim->depth = static_cast<std::uint8_t>(depth);
        victim->flags = 0U;
        write_best_action(*victim, best_action);
        merge(*victim, depth, exact, lower, upper, best_action);
    }
}

}  // namespace touniu_solver
