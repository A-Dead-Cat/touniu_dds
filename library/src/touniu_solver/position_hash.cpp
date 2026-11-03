#include "position_hash.hpp"

#include <algorithm>
#include <vector>

namespace touniu_solver {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

void mix(std::uint64_t& hash, const std::uint64_t value)
{
    hash ^= value;
    hash *= FnvPrime;
}

void mix_card(std::uint64_t& hash, const Position& position, const CardId id)
{
    mix(hash, id.value);
    const Card* card = position.card(id);
    if (card == nullptr) {
        return;
    }
    mix(hash, static_cast<std::uint64_t>(card->kind()));
    mix(hash, static_cast<std::uint64_t>(card->rank().millionths()));
    mix(hash, card->intrinsic_suits().size());
    for (const Suit suit : card->intrinsic_suits()) {
        mix(hash, static_cast<std::uint64_t>(suit));
    }
}

}  // namespace

auto hash_position(const Position& position) -> std::uint64_t
{
    std::uint64_t hash = FnvOffset;
    mix(hash, static_cast<std::uint64_t>(position.next_player()));
    mix(hash, static_cast<std::uint64_t>(position.direction()));
    mix(hash, static_cast<std::uint64_t>(position.contract()));
    mix(hash, position.tricks_remaining());
    mix(hash, position.seed());
    mix(hash, position.draw_index());
    mix(hash, position.valid_reverse_count());
    mix(hash, position.pending_block().has_value() ?
        static_cast<std::uint64_t>(*position.pending_block()) + 1U : 0U);

    for (int seat_value = 0; seat_value < 4; ++seat_value) {
        const Seat seat = static_cast<Seat>(seat_value);
        mix(hash, position.tricks_won(seat));
        std::vector<CardId> hand = position.hand(seat);
        std::sort(hand.begin(), hand.end(), [](const CardId left, const CardId right) {
            return left.value < right.value;
        });
        mix(hash, hand.size());
        for (const CardId id : hand) {
            mix_card(hash, position, id);
        }
    }

    mix(hash, position.current_trick().size());
    for (const TrickPlay& play : position.current_trick()) {
        mix(hash, static_cast<std::uint64_t>(play.seat));
        mix(hash, play.action.is_pass() ? 0U : 1U);
        if (!play.action.is_pass()) {
            mix_card(hash, position, *play.action.card_id());
            mix(hash, static_cast<std::uint64_t>(*play.action.mode()));
            mix(hash, static_cast<std::uint64_t>(*play.action.effective_suit()));
        }
    }
    return hash;
}

}  // namespace touniu_solver
