#include "live_position.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace touniu_solver {
namespace {

auto has_valid_catalog_ownership(const LivePosition& live) -> bool
{
    std::unordered_map<std::uint64_t, std::uint32_t> ownership;
    for (const Card& card : live.cards) {
        if (!ownership.emplace(card.id().value, 0U).second) {
            return false;
        }
    }

    const auto claim = [&ownership](const CardId id) {
        const auto found = ownership.find(id.value);
        if (found == ownership.end() || found->second != 0U) {
            return false;
        }
        ++found->second;
        return true;
    };

    for (const std::vector<CardId>& hand : live.hands) {
        for (const CardId id : hand) {
            if (!claim(id)) {
                return false;
            }
        }
    }

    for (const LiveTrickPlay& play : live.current_trick) {
        if (play.is_pass) {
            if (play.card_id.value != 0U || play.mode.has_value() ||
                play.effective_suit.has_value()) {
                return false;
            }
        } else if (!play.mode.has_value() || !play.effective_suit.has_value() ||
                   !claim(play.card_id)) {
            return false;
        }
    }

    for (const auto& entry : ownership) {
        if (entry.second != 1U) {
            return false;
        }
    }
    return true;
}

auto next_seat(const Seat seat, const Direction direction) -> Seat
{
    const int value = static_cast<int>(seat);
    return static_cast<Seat>((value + (direction == Direction::Forward ? 1 : 3)) % 4);
}

auto is_valid_mode(const PlayMode mode) -> bool
{
    return mode >= PlayMode::Lead && mode <= PlayMode::Trump;
}

auto is_valid_suit(const Suit suit) -> bool
{
    return suit >= Suit::Spades && suit <= Suit::NoTrump;
}

auto is_valid_seat(const Seat seat) -> bool
{
    return seat >= Seat::West && seat <= Seat::South;
}

auto has_valid_position_enums(const LivePosition& live) -> bool
{
    if (!is_valid_seat(live.next_player) ||
        (live.direction != Direction::Forward && live.direction != Direction::Reverse) ||
        live.contract < Contract::Spades || live.contract > Contract::NoTrump) {
        return false;
    }
    return std::all_of(
        live.current_trick.begin(),
        live.current_trick.end(),
        [](const LiveTrickPlay& play) { return is_valid_seat(play.seat); }
    );
}

auto find_card(const LivePosition& live, const CardId id) -> const Card*
{
    const auto found = std::find_if(
        live.cards.begin(),
        live.cards.end(),
        [id](const Card& card) { return card.id() == id; }
    );
    return found == live.cards.end() ? nullptr : &*found;
}

auto has_consistent_trick_state(const LivePosition& live) -> bool
{
    if (live.current_trick.size() > 3U ||
        (!live.current_trick.empty() && live.tricks_remaining == 0U)) {
        return false;
    }

    std::optional<Seat> expected_pending;
    std::uint32_t reverse_count = 0;
    for (std::size_t index = 0; index < live.current_trick.size(); ++index) {
        const LiveTrickPlay& play = live.current_trick[index];
        if (index > 0U && play.seat != next_seat(
                live.current_trick[index - 1U].seat, live.direction)) {
            return false;
        }
        if (expected_pending.has_value()) {
            if (play.seat != *expected_pending || !play.is_pass) {
                return false;
            }
            expected_pending = std::nullopt;
        }
        if (play.is_pass) {
            continue;
        }

        const PlayMode mode = *play.mode;
        const Suit effective_suit = *play.effective_suit;
        if (!is_valid_mode(mode) || !is_valid_suit(effective_suit)) {
            return false;
        }

        const Card* card = find_card(live, play.card_id);
        if (card == nullptr) {
            return false;
        }
        const bool effect_is_valid = mode != PlayMode::Discard;
        if (card->kind() == CardKind::Block && effect_is_valid) {
            expected_pending = next_seat(play.seat, live.direction);
        } else if (card->kind() == CardKind::Reverse && effect_is_valid) {
            ++reverse_count;
        }
    }

    if (!live.current_trick.empty() &&
        live.next_player != next_seat(live.current_trick.back().seat, live.direction)) {
        return false;
    }
    return expected_pending == live.pending_block &&
           reverse_count == live.valid_reverse_count;
}

auto has_valid_generation_provenance(const LivePosition& live) -> bool
{
    std::unordered_set<std::uint64_t> generated_ids;
    std::optional<std::uint64_t> previous;
    for (const CardId id : live.current_trick_generated_card_ids) {
        if (!generated_ids.insert(id.value).second || find_card(live, id) == nullptr) {
            return false;
        }
        // Generated IDs use an ever-increasing draw cursor, so a correctly
        // ordered provenance lists them in strictly ascending ID order. A
        // pre-existing catalog card may share the high ID range, but its ID
        // never regresses the sequence of freshly generated cards.
        if (previous.has_value() && id.value <= *previous) {
            return false;
        }
        previous = id.value;
    }
    return true;
}

}  // namespace

auto detail::has_valid_live_position_encoding(const LivePosition& live) -> bool
{
    return has_valid_position_enums(live) && has_valid_catalog_ownership(live) &&
           has_consistent_trick_state(live) && has_valid_generation_provenance(live) &&
           live.trick_start_draw_index <= live.draw_index;
}

}  // namespace touniu_solver
