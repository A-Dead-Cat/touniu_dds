#include "position.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <iterator>
#include <cstdio>

#include <touniu_solver/live_position.hpp>
#include <touniu_solver/trick_resolution.hpp>

namespace touniu_solver {
namespace {

auto seat_index(const Seat seat) -> std::size_t
{
    return static_cast<std::size_t>(seat);
}

bool compact_operations_disabled_for_testing = false;

constexpr std::uint32_t kNoUniverseSlot = 0xFFFFFFFFU;

auto is_effect_card(const CardKind kind) -> bool
{
    return kind == CardKind::Block || kind == CardKind::Reverse ||
           kind == CardKind::PlusTwo;
}

auto generated_during_current_trick(
    const CardId id,
    const LivePosition& live) -> bool
{
    return std::find(
        live.current_trick_generated_card_ids.begin(),
        live.current_trick_generated_card_ids.end(),
        id
    ) != live.current_trick_generated_card_ids.end();
}

auto cards_match(const Card& left, const Card& right) -> bool
{
    return left.id() == right.id() && left.kind() == right.kind() &&
           left.rank() == right.rank() &&
           left.intrinsic_suits() == right.intrinsic_suits();
}

auto matches_live_position(const Position& position, const LivePosition& live) -> bool
{
    if (position.next_player() != live.next_player ||
        position.direction() != live.direction ||
        position.contract() != live.contract ||
        position.tricks_remaining() != live.tricks_remaining ||
        position.seed() != live.seed || position.draw_index() != live.draw_index ||
        position.pending_block() != live.pending_block ||
        position.valid_reverse_count() != live.valid_reverse_count ||
        position.current_trick().size() != live.current_trick.size()) {
        return false;
    }
    for (std::size_t seat = 0; seat < live.hands.size(); ++seat) {
        const Seat seat_value = static_cast<Seat>(seat);
        if (position.hand(seat_value) != live.hands[seat] ||
            position.tricks_won(seat_value) != live.tricks_won[seat]) {
            return false;
        }
    }
    for (std::size_t index = 0; index < live.current_trick.size(); ++index) {
        const LiveTrickPlay& expected = live.current_trick[index];
        const TrickPlay& actual = position.current_trick()[index];
        const Action expected_action = expected.is_pass
            ? Action::pass()
            : Action::play(expected.card_id, *expected.mode, *expected.effective_suit);
        if (actual.seat != expected.seat || actual.action != expected_action) {
            return false;
        }
    }
    for (const Card& expected : live.cards) {
        const Card* actual = position.card(expected.id());
        if (actual == nullptr || !cards_match(*actual, expected)) {
            return false;
        }
    }
    return true;
}

}  // namespace
void set_compact_operations_disabled_for_testing(const bool disabled)
{
    compact_operations_disabled_for_testing = disabled;
}


Position::Position(State state) : state_(std::move(state))
{
    refresh_identity();
    rebuild_universe();
}

auto Position::create(const Scenario& scenario) -> std::optional<Position>
{
    auto state = State::create(scenario);
    if (!state.has_value()) {
        return std::nullopt;
    }
    return Position(std::move(*state));
}

auto Position::create(const LivePosition& live) -> std::optional<Position>
{
    if (!detail::has_valid_live_position_encoding(live)) {
        return std::nullopt;
    }

    std::array<std::vector<CardId>, 4> replay_hands;
    for (std::size_t seat = 0; seat < live.hands.size(); ++seat) {
        for (const CardId id : live.hands[seat]) {
            if (!generated_during_current_trick(id, live)) {
                replay_hands[seat].push_back(id);
            }
        }
    }
    for (const LiveTrickPlay& play : live.current_trick) {
        if (!play.is_pass && !generated_during_current_trick(play.card_id, live)) {
            replay_hands[seat_index(play.seat)].push_back(play.card_id);
        }
    }
    std::vector<Card> replay_cards;
    for (const Card& card : live.cards) {
        if (!generated_during_current_trick(card.id(), live)) {
            replay_cards.push_back(card);
        }
    }
    State replay_state(
        std::move(replay_hands),
        std::move(replay_cards),
        live.current_trick.empty() ? live.next_player : live.current_trick.front().seat,
        live.direction,
        live.contract,
        live.tricks_remaining,
        live.seed,
        live.trick_start_draw_index
    );
    Position replay(std::move(replay_state));
    replay.fast_active_ = false;
    replay.tricks_won_ = live.tricks_won;
    for (const LiveTrickPlay& play : live.current_trick) {
        const Action action = play.is_pass
            ? Action::pass()
            : Action::play(play.card_id, *play.mode, *play.effective_suit);
        const ActionList legal = replay.legal_actions();
        if (std::find(legal.begin(), legal.end(), action) == legal.end() ||
            !replay.apply(action)) {
            return std::nullopt;
        }
    }
    if (!matches_live_position(replay, live)) {
        return std::nullopt;
    }
    replay.refresh_identity();
    replay.rebuild_universe();
    replay.undo_stack_.clear();
    return replay;
}

auto Position::next_player() const -> Seat
{
    return state_.next_player();
}

auto Position::direction() const -> Direction
{
    return state_.direction();
}

auto Position::contract() const -> Contract
{
    return state_.contract();
}

auto Position::tricks_remaining() const -> std::uint32_t
{
    return state_.tricks_remaining();
}

auto Position::tricks_won(const Seat seat) const -> std::uint32_t
{
    return tricks_won_[seat_index(seat)];
}

auto Position::is_finished() const -> bool
{
    return state_.is_finished();
}

auto Position::current_trick() const -> const Trick&
{
    materialize_trick();
    return current_trick_;
}

auto Position::hand(const Seat seat) const -> const std::vector<CardId>&
{
    if (fast_active_) {
        materialize_hands();
        return hand_cache_[seat_index(seat)];
    }
    return state_.hand(seat);
}

auto Position::card(const CardId id) const -> const Card*
{
    return state_.card(id);
}

auto Position::card_catalog() const -> const std::vector<Card>&
{
    return state_.cards();
}

auto Position::draw_index() const -> std::uint64_t
{
    return state_.draw_index();
}

auto Position::seed() const -> std::uint32_t
{
    return state_.seed();
}

auto Position::valid_reverse_count() const -> std::uint32_t
{
    return valid_reverse_count_;
}

auto Position::pending_block() const -> std::optional<Seat>
{
    return pending_block_;
}

auto Position::identity() const -> const PositionIdentity&
{
    if (fast_active_ && !identity_valid_) {
        identity_ = identity_tokens::build_identity(identity_input());
        identity_valid_ = true;
    }
    return identity_;
}

auto Position::identity_input() const -> identity_tokens::IdentityInput
{
    identity_tokens::IdentityInput input;
    input.next_player = state_.next_player();
    input.direction = state_.direction();
    input.contract = state_.contract();
    input.tricks_remaining = state_.tricks_remaining();
    input.seed = state_.seed();
    input.draw_index = state_.draw_index();
    input.valid_reverse_count = valid_reverse_count_;
    input.pending_block = pending_block_;
    input.hands = fast_active_ ? &materialize_hands() : &state_.hands();
    input.tricks_won = &tricks_won_;
    input.catalog = &state_.cards();
    input.current_trick = &current_trick();
    return input;
}

void Position::refresh_identity()
{
    identity_ = identity_tokens::build_identity(identity_input());
    identity_valid_ = true;
}

auto recompute_position_identity(const Position& position) -> PositionIdentity
{
    return identity_tokens::build_identity(position.identity_input());
}

namespace {

auto is_plain_single_suit(const Card& card) -> bool
{
    return (card.kind() == CardKind::Normal || card.kind() == CardKind::Floating) &&
           card.intrinsic_suits().size() == 1U;
}

auto is_compact_single_family(const Card& card) -> bool
{
    return is_plain_single_suit(card) || card.kind() == CardKind::BigJoker ||
           card.kind() == CardKind::SmallJoker ||
           card.kind() == CardKind::NoTrump;
}

auto suit_index(const Suit suit) -> std::size_t
{
    return static_cast<std::size_t>(suit);
}

}  // namespace

auto Position::has_equivalence_universe() const -> bool
{
    return universe_.enabled;
}

auto Position::universe_bit(const CardId id, const Suit suit) const
    -> std::optional<std::uint8_t>
{
    const auto found = std::lower_bound(
        universe_.cards.begin(),
        universe_.cards.end(),
        id.value,
        [](const UniverseCard& card, const std::uint64_t value) {
            return card.id < value;
        });
    if (found == universe_.cards.end() || found->id != id.value) {
        return std::nullopt;
    }
    for (std::uint8_t index = 0; index < found->suit_count; ++index) {
        if (found->suits[index] == static_cast<std::uint8_t>(suit)) {
            return found->bits[index];
        }
    }
    return std::nullopt;
}

auto Position::universe_card(const std::uint64_t id) -> UniverseCard*
{
    const auto found = std::lower_bound(
        universe_.cards.begin(),
        universe_.cards.end(),
        id,
        [](const UniverseCard& card, const std::uint64_t value) {
            return card.id < value;
        });
    if (found == universe_.cards.end() || found->id != id) {
        return nullptr;
    }
    return &*found;
}

void Position::update_holdings(
    const Seat seat,
    const CardId id,
    const bool set)
{
    UniverseCard* card = universe_card(id.value);
    if (card == nullptr) {
        return;
    }
    std::array<std::uint64_t, 5>& target = holdings_[seat_index(seat)];
    const Card* definition = state_.card(id);
    if (definition != nullptr && definition->kind() == CardKind::Block) {
        std::uint32_t& blocks = block_cards_[seat_index(seat)];
        blocks = set ? blocks + 1U : blocks - 1U;
    }
    if (definition != nullptr &&
        (definition->kind() == CardKind::ThreeSuitCat ||
         definition->intrinsic_suits().size() == 4U)) {
        std::uint32_t& multi = multi_suit_cards_[seat_index(seat)];
        multi = set ? multi + 1U : multi - 1U;
    }
    // A four-suit or three-flower card is never obliged to follow a suit, so
    // only single-suit cards count toward "must follow".
    const bool real_suit_cards = definition != nullptr &&
        definition->intrinsic_suits().size() != 4U &&
        definition->kind() != CardKind::ThreeSuitCat;
    if (real_suit_cards) {
        for (std::uint8_t index = 0; index < card->suit_count; ++index) {
            const std::uint64_t bit = std::uint64_t{1} << card->bits[index];
            target[card->suits[index]] = set
                ? target[card->suits[index]] | bit
                : target[card->suits[index]] & ~bit;
            owner_of_[card->suits[index]][card->bits[index]] =
                set ? static_cast<std::uint8_t>(seat) : 0xFFU;
        }
    }
    if (card->trump_bit != 0xFFU) {
        const std::uint64_t bit = std::uint64_t{1} << card->trump_bit;
        target[4] = set ? target[4] | bit : target[4] & ~bit;
        owner_of_[4][card->trump_bit] =
            set ? static_cast<std::uint8_t>(seat) : 0xFFU;
    }
}

void Position::rebuild_universe()
{
    universe_ = EquivalenceUniverse{};
    live_mask_ = {};
    holdings_ = {};
    block_cards_ = {};
    multi_suit_cards_ = {};
    for (auto& bucket : owner_of_) {
        bucket.fill(0xFFU);
    }
    shareable_identity_ = false;

    std::size_t plus_two_count = 0;
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : state_.hand(static_cast<Seat>(seat))) {
            const Card* card = state_.card(id);
            if (card != nullptr && card->kind() == CardKind::PlusTwo) {
                ++plus_two_count;
            }
        }
    }

    // The draw sequence is a deterministic function of the seed, the draw
    // cursor and the cards already present, so simulating it here yields every
    // floating card this deal can still produce, in every play order.
    std::vector<Card> predicted;
    if (plus_two_count > 0) {
        State generator = state_;
        const std::size_t limit = plus_two_count * 2U;
        predicted.reserve(limit);
        for (std::size_t index = 0; index < limit; ++index) {
            predicted.push_back(generator.draw_floating(Seat::West));
            universe_.predicted_ids.push_back(predicted.back().id().value);
        }
    }

    std::vector<CardId> live;
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : state_.hand(static_cast<Seat>(seat))) {
            live.push_back(id);
        }
    }
    for (const TrickPlay& play : current_trick_) {
        if (!play.action.is_pass()) {
            live.push_back(*play.action.card_id());
        }
    }

    std::array<std::vector<std::pair<std::int32_t, std::uint64_t>>, 4> entries;
    std::vector<std::pair<std::int32_t, std::uint64_t>> trump_entries;
    const auto add_capable = [this, &entries, &trump_entries](const Card& card) {
        // A three-flower cat ranks below every real card in no-trump, so the
        // universe must compare it with the rank trick resolution uses.
        const auto rank =
            state_.contract() == Contract::NoTrump &&
                card.kind() == CardKind::ThreeSuitCat
            ? 999'999 : card.rank().millionths();
        const std::uint64_t id = card.id().value;
        if (is_plain_single_suit(card)) {
            entries[suit_index(card.intrinsic_suits().front())].push_back({rank, id});
            return;
        }
        if (card.kind() == CardKind::BigJoker || card.kind() == CardKind::SmallJoker) {
            trump_entries.push_back({rank, id});
            return;
        }
        if (card.intrinsic_suits().size() == 4U ||
            card.kind() == CardKind::ThreeSuitCat) {
            for (std::size_t suit = 0; suit < 4; ++suit) {
                entries[suit].push_back({rank, id});
            }
            return;
        }
        if (card.intrinsic_suits().size() == 1U &&
            card.intrinsic_suits().front() != Suit::NoTrump) {
            entries[suit_index(card.intrinsic_suits().front())].push_back({rank, id});
            return;
        }
        // NoTrump-suit cards and NoTrump-suit specials are trump in no-trump
        // contracts, so no ordinary top card is a sure trick while they live.
        trump_entries.push_back({rank, id});
    };

    for (const CardId id : live) {
        const Card* card = state_.card(id);
        if (card != nullptr) {
            add_capable(*card);
        }
    }
    for (const Card& card : predicted) {
        add_capable(card);
    }

    for (std::size_t suit = 0; suit < 4; ++suit) {
        std::vector<std::pair<std::int32_t, std::uint64_t>>& list = entries[suit];
        std::sort(list.begin(), list.end());
        list.erase(
            std::unique(
                list.begin(),
                list.end(),
                [](const auto& left, const auto& right) {
                    return left.second == right.second;
                }),
            list.end());
        if (list.size() > 64U) {
            universe_ = EquivalenceUniverse{};
            live_mask_ = {};
            return;
        }
        universe_.ranks[suit].resize(list.size());
        universe_.ids[suit].resize(list.size());
        for (std::size_t index = 0; index < list.size(); ++index) {
            universe_.ranks[suit][index] = {
                list[index].first, static_cast<std::uint8_t>(index)};
            universe_.ids[suit][index] = list[index].second;
            {
                UniverseCard card;
                card.id = list[index].second;
                const auto position = std::lower_bound(
                    universe_.cards.begin(),
                    universe_.cards.end(),
                    card.id,
                    [](const UniverseCard& existing, const std::uint64_t value) {
                        return existing.id < value;
                    });
                universe_.cards.insert(position, card);
            }
        }
        universe_.prefix_masks[suit].assign(list.size() + 1U, 0U);
        for (std::size_t index = 0; index < list.size(); ++index) {
            universe_.prefix_masks[suit][index + 1U] =
                universe_.prefix_masks[suit][index] |
                (std::uint64_t{1} << static_cast<std::uint8_t>(index));
        }
        universe_.masks[suit] = universe_.prefix_masks[suit].back();
    }

    for (std::size_t suit = 0; suit < 4; ++suit) {
        for (std::size_t index = 0; index < entries[suit].size(); ++index) {
            UniverseCard* card = universe_card(entries[suit][index].second);
            const std::size_t slot = card->suit_count;
            card->suits[slot] = static_cast<std::uint8_t>(suit);
            card->bits[slot] = static_cast<std::uint8_t>(index);
            card->suit_count = static_cast<std::uint8_t>(slot + 1U);
        }
    }

    // Bits follow rank order so the highest live trump is the highest set bit.
    std::sort(trump_entries.begin(), trump_entries.end());
    trump_entries.erase(
        std::unique(
            trump_entries.begin(),
            trump_entries.end(),
            [](const auto& left, const auto& right) {
                return left.second == right.second;
            }),
        trump_entries.end());
    if (trump_entries.size() > 64U) {
        universe_.trump_overflow = true;
    } else {
        universe_.trump_cards.resize(trump_entries.size());
        universe_.trump_ranks.resize(trump_entries.size());
        for (std::size_t index = 0; index < trump_entries.size(); ++index) {
            universe_.trump_cards[index] = trump_entries[index].second;
            universe_.trump_ranks[index] = trump_entries[index].first;
            {
                UniverseCard entry;
                entry.id = trump_entries[index].second;
                const auto position = std::lower_bound(
                    universe_.cards.begin(),
                    universe_.cards.end(),
                    entry.id,
                    [](const UniverseCard& existing, const std::uint64_t value) {
                        return existing.id < value;
                    });
                if (position == universe_.cards.end() || position->id != entry.id) {
                    universe_.cards.insert(position, entry);
                }
            }
            UniverseCard* card = universe_card(trump_entries[index].second);
            if (card != nullptr) {
                card->trump_bit = static_cast<std::uint8_t>(index);
                const Card* definition =
                    state_.card(CardId{trump_entries[index].second});
                if (definition != nullptr &&
                    (definition->kind() == CardKind::BigJoker ||
                     definition->kind() == CardKind::SmallJoker)) {
                    universe_.joker_mask |=
                        std::uint64_t{1} << static_cast<std::uint8_t>(index);
                    universe_.nt_trump_mask |=
                        std::uint64_t{1} << static_cast<std::uint8_t>(index);
                } else if (definition != nullptr &&
                           definition->kind() == CardKind::NoTrump) {
                    universe_.nt_trump_mask |=
                        std::uint64_t{1} << static_cast<std::uint8_t>(index);
                }
            }
            universe_.masks[4] |= std::uint64_t{1} << static_cast<std::uint8_t>(index);
        }
    }

    live_mask_ = universe_.masks;
    universe_.enabled = true;
    shareable_identity_ = universe_.predicted_ids.empty();
    for (const UniverseCard& entry : universe_.cards) {
        const Card* definition = state_.card(CardId{entry.id});
        if (definition == nullptr || !is_compact_single_family(*definition)) {
            shareable_identity_ = false;
            break;
        }
    }
    for (int seat = 0; seat < 4 && shareable_identity_; ++seat) {
        for (const CardId id : state_.hand(static_cast<Seat>(seat))) {
            const Card* definition = state_.card(id);
            if (definition == nullptr || !is_compact_single_family(*definition)) {
                shareable_identity_ = false;
                break;
            }
        }
    }
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : state_.hand(static_cast<Seat>(seat))) {
            update_holdings(static_cast<Seat>(seat), id, true);
        }
    }
    build_fast_state();
}

void Position::retire_trick_cards(const Trick& trick)
{
    if (!universe_.enabled) {
        return;
    }
    for (const TrickPlay& play : trick) {
        if (play.action.is_pass()) {
            continue;
        }
        UniverseCard* card = universe_card(play.action.card_id()->value);
        if (card == nullptr) {
            continue;
        }
        for (std::uint8_t index = 0; index < card->suit_count; ++index) {
            live_mask_[card->suits[index]] &=
                ~(std::uint64_t{1} << card->bits[index]);
        }
        if (card->trump_bit != 0xFFU) {
            live_mask_[4] &= ~(std::uint64_t{1} << card->trump_bit);
        }
    }
}

void Position::prune_reached_predictions()
{
    if (!universe_.enabled || universe_.predicted_ids.empty()) {
        return;
    }
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : state_.hand(static_cast<Seat>(seat))) {
            const Card* card = state_.card(id);
            if (card != nullptr && card->kind() == CardKind::PlusTwo) {
                return;
            }
        }
    }
    // No +2 can generate anything any more; predicted cards that never appeared
    // will never appear, so their bits can stop blocking merges.
    for (const std::uint64_t id : universe_.predicted_ids) {
        if (state_.card(CardId{id}) != nullptr) {
            continue;
        }
        UniverseCard* card = universe_card(id);
        if (card == nullptr) {
            continue;
        }
        for (std::uint8_t index = 0; index < card->suit_count; ++index) {
            live_mask_[card->suits[index]] &=
                ~(std::uint64_t{1} << card->bits[index]);
        }
    }
}

auto Position::quick_rank(const CardId id) const -> std::int32_t
{
    if (fast_active_) {
        const std::uint32_t slot = fast_slot(id);
        if (slot != 0xFFFFFFFFU) {
            return fast_rank(
                static_cast<std::uint8_t>(slot >> 8U),
                static_cast<std::uint8_t>(slot & 0xFFU));
        }
    }
    const Card* card = state_.card(id);
    return card == nullptr ? -1 : card->rank().millionths();
}

auto Position::uses_compact_operations() const -> bool
{
    return fast_active_;
}

auto Position::compact_plain_tail() const -> std::optional<Position>
{
    if (fast_active_ || !current_trick_.empty() || pending_block_.has_value() ||
        valid_reverse_count_ != 0U) {
        return std::nullopt;
    }
    for (int seat = 0; seat < 4; ++seat) {
        const auto& cards = state_.hand(static_cast<Seat>(seat));
        if (cards.size() > fast_order_[seat].size()) {
            return std::nullopt;
        }
        for (const CardId id : cards) {
            const Card* definition = state_.card(id);
            if (definition == nullptr || !is_compact_single_family(*definition)) {
                return std::nullopt;
            }
        }
    }

    Position compact = *this;
    compact.undo_stack_.clear();
    compact.fast_undo_.clear();
    compact.rebuild_universe();
    if (!compact.fast_active_) {
        return std::nullopt;
    }
    return compact;
}

auto Position::hand_size(const Seat seat) const -> std::uint32_t
{
    return fast_active_
        ? fast_sizes_[seat_index(seat)]
        : static_cast<std::uint32_t>(state_.hand(seat).size());
}

auto Position::fast_slot(const CardId id) const -> std::uint32_t
{
    std::size_t slot = (id.value * 0x9E3779B97F4A7C15ULL) >> 56U;
    while (true) {
        const std::uint32_t entry = slot_index_[slot];
        if (entry == 0U) {
            return kNoUniverseSlot;
        }
        const std::uint32_t value = entry - 1U;
        const std::size_t bucket = value >> 8U;
        const std::size_t bit = value & 0xFFU;
        const std::uint64_t candidate = bucket == 4U
            ? universe_.trump_cards[bit] : universe_.ids[bucket][bit];
        if (candidate == id.value) {
            return value;
        }
        slot = (slot + 1U) & 0xFFU;
    }
}

auto Position::fast_rank(
    const std::uint8_t bucket,
    const std::uint8_t bit) const -> std::int32_t
{
    return bucket == 4U
        ? universe_.trump_ranks[bit] : universe_.ranks[bucket][bit].first;
}

auto Position::materialize_hands() const
    -> const std::array<std::vector<CardId>, 4>&
{
    if (!fast_active_ || hand_cache_valid_) {
        return hand_cache_;
    }
    for (std::size_t seat = 0; seat < 4; ++seat) {
        hand_cache_[seat].clear();
        const std::uint32_t count = fast_order_count_[seat];
        for (std::uint32_t index = 0; index < count; ++index) {
            const std::uint32_t slot = fast_order_[seat][index];
            const std::uint8_t bucket = static_cast<std::uint8_t>(slot >> 8U);
            const std::uint8_t bit = static_cast<std::uint8_t>(slot & 0xFFU);
            if (((fast_hands_[seat][bucket] >> bit) & 1U) == 0U) {
                continue;
            }
            hand_cache_[seat].push_back(CardId{bucket == 4U
                ? universe_.trump_cards[bit] : universe_.ids[bucket][bit]});
        }
    }
    hand_cache_valid_ = true;
    return hand_cache_;
}

void Position::materialize_trick() const
{
    if (!fast_active_) {
        return;
    }
    current_trick_.assign(
        fast_trick_.begin(),
        fast_trick_.begin() + static_cast<std::ptrdiff_t>(fast_trick_size_));
}

void Position::build_fast_state()
{
    fast_active_ = false;
    fast_hands_ = {};
    fast_sizes_ = {};
    fast_trick_ = {};
    fast_trick_size_ = 0;
    fast_undo_.clear();
    slot_index_.fill(0U);
    hand_cache_valid_ = false;
    identity_valid_ = true;
    for (auto& order : fast_order_) {
        order.fill(0U);
    }
    if (!has_shareable_identity() || compact_operations_disabled_for_testing) {
        return;
    }
    for (std::size_t bucket = 0; bucket < 5U; ++bucket) {
        const std::size_t count = bucket == 4U
            ? universe_.trump_cards.size() : universe_.ids[bucket].size();
        for (std::size_t bit = 0; bit < count; ++bit) {
            const std::uint64_t id = bucket == 4U
                ? universe_.trump_cards[bit] : universe_.ids[bucket][bit];
            std::size_t slot = (id * 0x9E3779B97F4A7C15ULL) >> 56U;
            while (slot_index_[slot] != 0U) {
                slot = (slot + 1U) & 0xFFU;
            }
            slot_index_[slot] =
                static_cast<std::uint32_t>((bucket << 8U) | bit) + 1U;
        }
    }
    for (std::size_t seat = 0; seat < 4; ++seat) {
        std::uint32_t order_index = 0;
        for (const CardId id : state_.hand(static_cast<Seat>(seat))) {
            const std::uint32_t slot = fast_slot(id);
            if (slot == kNoUniverseSlot ||
                order_index >= fast_order_[seat].size()) {
                return;
            }
            fast_order_[seat][order_index++] = slot;
            const std::uint8_t bucket = static_cast<std::uint8_t>(slot >> 8U);
            const std::uint8_t bit = static_cast<std::uint8_t>(slot & 0xFFU);
            fast_hands_[seat][bucket] |= std::uint64_t{1} << bit;
        }
        fast_sizes_[seat] = order_index;
        fast_order_count_[seat] = order_index;
    }
    // An imported live position can start mid-trick; the compact trick must
    // mirror the plays the replay already applied.
    fast_trick_size_ = static_cast<std::uint32_t>(
        current_trick_.size() < 4U ? current_trick_.size() : 4U);
    for (std::uint32_t index = 0; index < fast_trick_size_; ++index) {
        fast_trick_[index] = current_trick_[index];
    }
    fast_active_ = true;
}

void Position::legal_actions_fast(ActionList& out) const
{
    out.clear();
    if (state_.is_finished()) {
        return;
    }
    const std::size_t index = seat_index(state_.next_player());
    if (fast_sizes_[index] == 0U) {
        out.push_back(Action::pass());
        return;
    }
    const auto emit = [&](const std::uint32_t order_slot,
                          const PlayMode mode, const Suit suit) {
        const std::uint8_t bucket = static_cast<std::uint8_t>(order_slot >> 8U);
        const std::uint8_t bit = static_cast<std::uint8_t>(order_slot & 0xFFU);
        out.push_back(Action::play(
            CardId{bucket == 4U
                ? universe_.trump_cards[bit] : universe_.ids[bucket][bit]},
            mode, suit));
    };
    const bool no_trump = state_.contract() == Contract::NoTrump;
    const std::size_t trump_index = no_trump ? 4U
        : suit_index(static_cast<Suit>(static_cast<int>(state_.contract())));
    // Jokers and NT cards share bucket four. In a suit contract jokers join
    // the contract trump family while NT cards remain the fifth suit; in a
    // no-trump contract both belong to the NT trump family.
    const auto effective_family = [this, no_trump, trump_index](
                                      const std::size_t bucket,
                                      const std::uint8_t bit) {
        if (bucket != 4U || no_trump) {
            return bucket;
        }
        const bool joker =
            (universe_.joker_mask & (std::uint64_t{1} << bit)) != 0U;
        return joker ? trump_index : 4U;
    };
    const std::uint32_t count = fast_order_count_[index];
    if (fast_trick_size_ == 0U) {
        for (std::uint32_t order = 0; order < count; ++order) {
            const std::uint32_t slot = fast_order_[index][order];
            const std::uint8_t bucket = static_cast<std::uint8_t>(slot >> 8U);
            const std::uint8_t bit = static_cast<std::uint8_t>(slot & 0xFFU);
            if (((fast_hands_[index][bucket] >> bit) & 1U) == 0U) {
                continue;
            }
            emit(
                slot, PlayMode::Lead,
                static_cast<Suit>(effective_family(bucket, bit)));
        }
        return;
    }
    std::size_t led_index = 4U;
    for (std::uint32_t play = 0; play < fast_trick_size_; ++play) {
        if (!fast_trick_[play].action.is_pass()) {
            led_index = suit_index(*fast_trick_[play].action.effective_suit());
            break;
        }
    }
    bool has_led_suit = false;
    if (led_index == 4U) {
        const std::uint64_t nt_cards = no_trump
            ? fast_hands_[index][4]
            : fast_hands_[index][4] & ~universe_.joker_mask;
        has_led_suit = nt_cards != 0U;
    } else {
        has_led_suit = fast_hands_[index][led_index] != 0U;
        if (led_index == trump_index) {
            has_led_suit = has_led_suit ||
                (fast_hands_[index][4] & universe_.joker_mask) != 0U;
        }
    }
    for (std::uint32_t order = 0; order < count; ++order) {
        const std::uint32_t slot = fast_order_[index][order];
        const std::uint8_t bucket = static_cast<std::uint8_t>(slot >> 8U);
        const std::uint8_t bit = static_cast<std::uint8_t>(slot & 0xFFU);
        if (((fast_hands_[index][bucket] >> bit) & 1U) == 0U) {
            continue;
        }
        if (has_led_suit) {
            if (effective_family(bucket, bit) != led_index) {
                continue;
            }
            emit(slot, PlayMode::Follow, static_cast<Suit>(led_index));
        } else {
            const std::size_t family = effective_family(bucket, bit);
            const bool trump = family == trump_index;
            emit(slot, trump ? PlayMode::Trump : PlayMode::Discard,
                 static_cast<Suit>(family));
        }
    }
}

auto Position::resolve_trick_fast() const -> Seat
{
    std::size_t led_index = 4U;
    for (std::uint32_t play = 0; play < 4U; ++play) {
        if (!fast_trick_[play].action.is_pass()) {
            led_index = suit_index(*fast_trick_[play].action.effective_suit());
            break;
        }
    }
    const std::size_t trump_index = state_.contract() == Contract::NoTrump ? 4U
        : suit_index(static_cast<Suit>(static_cast<int>(state_.contract())));
    Seat winner = fast_trick_[0].seat;
    int best_class = -1;
    std::int32_t best_rank = -1;
    for (std::uint32_t play = 0; play < 4U; ++play) {
        const TrickPlay& entry = fast_trick_[play];
        if (entry.action.is_pass()) {
            continue;
        }
        const std::uint32_t slot = fast_slot(*entry.action.card_id());
        const std::uint8_t bucket = static_cast<std::uint8_t>(slot >> 8U);
        const std::uint8_t bit = static_cast<std::uint8_t>(slot & 0xFFU);
        const bool joker = bucket == 4U &&
            (universe_.joker_mask & (std::uint64_t{1} << bit)) != 0U;
        const std::size_t family = bucket == 4U && joker
            ? trump_index : bucket;
        const int card_class = family == trump_index
            ? 3 : (family == led_index ? 2 : 0);
        const std::int32_t rank = fast_rank(bucket, bit);
        if (card_class > best_class ||
            (card_class == best_class && rank > best_rank)) {
            best_class = card_class;
            best_rank = rank;
            winner = entry.seat;
        }
    }
    return winner;
}

void Position::apply_fast(const Action& action)
{
    FastUndo record;
    record.seat = static_cast<std::uint8_t>(seat_index(state_.next_player()));
    record.previous_next_player = state_.next_player();
    record.previous_trick_size = fast_trick_size_;
    record.pass = action.is_pass();
    if (!record.pass) {
        const std::uint32_t slot = fast_slot(*action.card_id());
        record.bucket = static_cast<std::uint8_t>(slot >> 8U);
        record.bit = static_cast<std::uint8_t>(slot & 0xFFU);
        const std::uint64_t mask = std::uint64_t{1} << record.bit;
        fast_hands_[record.seat][record.bucket] &= ~mask;
        --fast_sizes_[record.seat];
        holdings_[record.seat][record.bucket] &= ~mask;
        owner_of_[record.bucket][record.bit] = 0xFFU;
        hand_cache_valid_ = false;
    }
    fast_trick_[fast_trick_size_++] = {state_.next_player(), action};
    record.completed = fast_trick_size_ == 4U;
    if (record.completed) {
        record.previous_trick = fast_trick_;
        record.previous_live_mask = live_mask_;
        record.previous_tricks_remaining = state_.tricks_remaining();
        record.previous_tricks_won = tricks_won_;
        finish_trick_fast(record);
    } else {
        set_next_player(next_seat(state_.next_player()));
    }
    identity_valid_ = false;
    fast_undo_.push_back(std::move(record));
}

void Position::finish_trick_fast(FastUndo& record)
{
    (void) record;
    const Seat winner = resolve_trick_fast();
    for (std::uint32_t play = 0; play < 4U; ++play) {
        const TrickPlay& entry = fast_trick_[play];
        if (entry.action.is_pass()) {
            continue;
        }
        const std::uint32_t slot = fast_slot(*entry.action.card_id());
        live_mask_[slot >> 8U] &=
            ~(std::uint64_t{1} << static_cast<std::uint8_t>(slot & 0xFFU));
    }
    ++tricks_won_[seat_index(winner)];
    if (state_.tricks_remaining() > 0U) {
        set_tricks_remaining(state_.tricks_remaining() - 1U);
    }
    set_next_player(winner);
    fast_trick_size_ = 0;
}

void Position::undo_fast()
{
    FastUndo record = std::move(fast_undo_.back());
    fast_undo_.pop_back();
    if (!record.pass) {
        const std::uint64_t mask = std::uint64_t{1} << record.bit;
        fast_hands_[record.seat][record.bucket] |= mask;
        ++fast_sizes_[record.seat];
        holdings_[record.seat][record.bucket] |= mask;
        owner_of_[record.bucket][record.bit] = record.seat;
        hand_cache_valid_ = false;
    }
    if (record.completed) {
        fast_trick_ = record.previous_trick;
        live_mask_ = record.previous_live_mask;
        state_.set_tricks_remaining(record.previous_tricks_remaining);
        tricks_won_ = record.previous_tricks_won;
    }
    fast_trick_size_ = record.previous_trick_size;
    state_.set_next_player(record.previous_next_player);
    identity_valid_ = false;
}

auto Position::leader_sure_tricks() const -> SureTricks
{
    SureTricks result;
    const bool trick_empty = fast_active_ ? fast_trick_size_ == 0U
                                          : current_trick_.empty();
    if (!trick_empty || pending_block_.has_value() ||
        !universe_.enabled || universe_.trump_overflow) {
        return result;
    }
    const bool no_trump = state_.contract() == Contract::NoTrump;
    const std::size_t contract_trump = suit_index(
        static_cast<Suit>(static_cast<int>(state_.contract())));
    const Seat leader = state_.next_player();
    result.north_south = leader == Seat::North || leader == Seat::South;
    const auto leader_index = seat_index(leader);
    const auto partner = static_cast<Seat>(
        (static_cast<int>(leader) + 2) % 4);
    const auto partner_index = seat_index(partner);
    const std::array<std::uint64_t, 5>& leader_holding =
        holdings_[leader_index];
    const std::array<std::uint64_t, 5>& partner_holding =
        holdings_[partner_index];
    const auto top_bit = [](const std::uint64_t live) {
        return std::uint64_t{1} <<
            static_cast<std::uint8_t>(63 - std::countl_zero(live));
    };

    // An opponent who cannot follow the led suit can only take the trick with
    // a trump card. In no-trump only the trump bucket carries those; in a suit
    // contract the trump suit and multi-suit cards do too.
    const auto can_ruff = [&](const std::size_t seat) {
        if (no_trump) {
            return holdings_[seat][4] != 0;
        }
        return holdings_[seat][contract_trump] != 0 ||
            holdings_[seat][4] != 0 || multi_suit_cards_[seat] != 0;
    };

    // A block card played by an opponent in the led suit forces the next seat
    // to pass, which can rob the partner of the card that would win the trick.
    // Leader-held tops are led first and cannot be affected, so only partner
    // tops are withdrawn when an opponent still holds a block card.
    bool opponent_block = false;
    for (std::size_t seat = 0; seat < 4; ++seat) {
        if (seat == leader_index || seat == partner_index) {
            continue;
        }
        if (block_cards_[seat] != 0U) {
            opponent_block = true;
        }
    }

    // The leader cashes its own tops and keeps the lead for the trump, then
    // the partner wins the first of its tops and leads the remaining ones.
    std::uint32_t leader_tops = 0;
    std::uint32_t partner_tops = 0;
    for (std::size_t suit = 0; suit < 4; ++suit) {
        // A trump-suit top is cashed as the trump trick below, where it is
        // compared with the jokers.
        if (!no_trump && suit == contract_trump) {
            continue;
        }
        const std::uint64_t live = live_mask_[suit];
        if (live == 0) {
            continue;
        }
        const int bit = 63 - std::countl_zero(live);
        const std::uint64_t mask = std::uint64_t{1} << bit;
        const std::uint64_t id =
            universe_.ids[suit][static_cast<std::size_t>(bit)];
        const Card* card = state_.card(CardId{id});
        if (card == nullptr || !is_plain_single_suit(*card)) {
            continue;
        }
        const bool leader_top = (leader_holding[suit] & mask) != 0;
        const bool partner_top = (partner_holding[suit] & mask) != 0;
        if (!leader_top && !partner_top) {
            continue;
        }
        // Whoever leads the suit must hold a real card of it.
        if (leader_holding[suit] == 0) {
            continue;
        }
        bool no_ruff = true;
        bool all_follow = true;
        for (std::size_t seat = 0; seat < 4; ++seat) {
            if (seat == leader_index || seat == partner_index) {
                continue;
            }
            if (holdings_[seat][suit] == 0) {
                all_follow = false;
                if (can_ruff(seat)) {
                    no_ruff = false;
                }
            }
        }
        if (!no_ruff) {
            continue;
        }
        // Cards of one side's hand that outrank every opponent card of the
        // suit and can be cashed back to back: the owner keeps the lead after
        // each win, and every round costs a ruff-capable opponent one card of
        // the suit before it could ruff.
        const auto cashable_run = [&](const std::uint64_t owner_mask) {
            std::uint64_t opponent_cards = 0;
            for (std::size_t seat = 0; seat < 4; ++seat) {
                if (seat == leader_index || seat == partner_index) {
                    continue;
                }
                opponent_cards |= holdings_[seat][suit];
            }
            std::uint32_t run = 0;
            if (opponent_cards == 0) {
                run = static_cast<std::uint32_t>(std::popcount(owner_mask));
            } else {
                const int opponent_bit = 63 - std::countl_zero(opponent_cards);
                const std::uint64_t above = owner_mask &
                    ~((std::uint64_t{2} << static_cast<std::uint8_t>(
                        opponent_bit)) - 1U);
                run = static_cast<std::uint32_t>(std::popcount(above));
            }
            for (std::size_t seat = 0; seat < 4; ++seat) {
                if (seat == leader_index || seat == partner_index) {
                    continue;
                }
                if (can_ruff(seat)) {
                    run = std::min(run, static_cast<std::uint32_t>(
                        std::popcount(holdings_[seat][suit])));
                }
            }
            return run;
        };
        if (leader_top) {
            // The leader plays first, so an equal rank still wins.
            leader_tops += cashable_run(leader_holding[suit]);
        } else if (all_follow && !opponent_block) {
            // The partner plays after the opponents, so the top must be
            // strictly above every other live card of the suit.
            const std::uint64_t below = live & ~mask;
            bool strict = true;
            if (below != 0) {
                const std::size_t next_bit =
                    static_cast<std::size_t>(63 - std::countl_zero(below));
                strict = universe_.ranks[suit][next_bit].first !=
                    universe_.ranks[suit][static_cast<std::size_t>(bit)].first;
            }
            if (strict) {
                // The partner wins the first round with the suit's top card
                // and keeps the lead for the rest of the run.
                partner_tops += cashable_run(partner_holding[suit]);
            }
        }
    }

    // The leader's highest trump wins any trump lead. In a suit contract the
    // jokers and NoTrump-kind cards outrank the trump suit, while NoTrump-suit
    // specials are not trump there at all.
    std::uint32_t trump_trick = 0;
    if (no_trump) {
        const std::uint64_t live_trumps = live_mask_[4];
        if (live_trumps != 0 &&
            (leader_holding[4] & top_bit(live_trumps)) != 0) {
            trump_trick = 1;
        }
    } else {
        // The highest trump-capable card is the better of the highest
        // joker/NoTrump-kind card and the highest card of the trump suit.
        std::uint64_t best_mask = 0;
        std::size_t best_bucket = 0;
        std::int32_t best_rank = -1;
        const std::uint64_t jokers = live_mask_[4] & universe_.joker_mask;
        if (jokers != 0) {
            const std::size_t bit =
                static_cast<std::size_t>(63 - std::countl_zero(jokers));
            best_rank = universe_.trump_ranks[bit];
            best_mask = std::uint64_t{1} << static_cast<std::uint8_t>(bit);
            best_bucket = 4;
        }
        const std::uint64_t trumps = live_mask_[contract_trump];
        if (trumps != 0) {
            const std::size_t bit =
                static_cast<std::size_t>(63 - std::countl_zero(trumps));
            const std::int32_t rank =
                universe_.ranks[contract_trump][bit].first;
            // Only an ordinary single-suit card wins a trump lead: a cherry
            // bomb hands the trick to the next seat, and a cat or four-suit
            // card cannot be led as the trump suit.
            const Card* top = state_.card(
                CardId{universe_.ids[contract_trump][bit]});
            if (top != nullptr && !is_plain_single_suit(*top)) {
                best_rank = 16'000'000;
            } else if (rank > best_rank) {
                best_rank = rank;
                best_mask = std::uint64_t{1} << static_cast<std::uint8_t>(bit);
                best_bucket = contract_trump;
            }
        }
        if (best_mask != 0 &&
            (leader_holding[best_bucket] & best_mask) != 0) {
            // A three-flower cat or a four-suit card can be played as the
            // trump suit and outranks an ordinary trump, so only the top rank
            // (jokers and NoTrump-kind cards at 16) is safe on its own.
            if (best_bucket == 4 && best_rank >= 16'000'000) {
                trump_trick = 1;
            } else {
                bool multi_suit_opponent = false;
                for (std::size_t seat = 0; seat < 4; ++seat) {
                    if (seat == leader_index || seat == partner_index) {
                        continue;
                    }
                    if (multi_suit_cards_[seat] != 0U) {
                        multi_suit_opponent = true;
                    }
                }
                if (!multi_suit_opponent) {
                    trump_trick = 1;
                }
            }
        }
    }

    // The partner keeps its own tops for later tricks; every other counted
    // trick must be covered by one of its spare cards, because a forced
    // discard could otherwise throw a needed top away.
    const std::uint32_t partner_cards = hand_size(partner);
    const std::uint32_t partner_other_plays = leader_tops + trump_trick;
    std::uint32_t counted_partner_tops = 0;
    if (partner_cards >= partner_other_plays) {
        counted_partner_tops = std::min(
            partner_tops, partner_cards - partner_other_plays);
    }
    // The leader plays one card on every counted trick.
    std::uint32_t count = leader_tops + counted_partner_tops + trump_trick;
    count = std::min(count, hand_size(leader));
    count = std::min(count, state_.tricks_remaining());
    result.tricks = count;
    return result;
}

auto Position::has_shareable_identity() const -> bool
{
    return universe_.enabled && shareable_identity_;
}

auto Position::table_identity() const -> PositionIdentity
{
    if (!has_shareable_identity()) {
        return identity();
    }
    const Trick& trick = current_trick();
    // Only the relative order of the live cards and their owners matters, so
    // the key walks the live buckets in rank order and records the run of
    // owners. Retired cards vanish from the walk, which is what lets two
    // positions reached by playing interchangeable cards share an entry.
    std::uint64_t primary = 0x9E3779B97F4A7C15ULL;
    std::uint64_t verifier = 0xC2B2AE3D27D4EB4FULL;
    const auto mix = [](std::uint64_t& left, std::uint64_t& right,
                        const std::uint64_t value) {
        left = (left ^ value) * 0x9E3779B97F4A7C15ULL;
        right = (right + value) * 0xBF58476D1CE4E5B9ULL;
    };
    for (std::size_t bucket = 0; bucket < 5; ++bucket) {
        std::uint64_t live = live_mask_[bucket];
        std::uint32_t dense = 0;
        while (live != 0) {
            const int bit = 63 - std::countl_zero(live);
            live &= ~(std::uint64_t{1} << static_cast<std::uint8_t>(bit));
            // Bucket four can contain two different effective families in a
            // suit contract: jokers belong to trumps while NT cards remain
            // the independent fifth suit. Preserve its exact universe bit so
            // retiring one family can never alias a state from the other.
            const std::uint32_t rank_token = bucket == 4U
                ? static_cast<std::uint32_t>(bit) : dense;
            const std::uint64_t token =
                (static_cast<std::uint64_t>(bucket) << 32U) |
                (static_cast<std::uint64_t>(rank_token) << 8U) |
                owner_of_[bucket][static_cast<std::size_t>(bit)];
            mix(primary, verifier, token);
            ++dense;
        }
    }
    mix(primary, verifier,
        (static_cast<std::uint64_t>(state_.next_player()) << 40U) |
        (static_cast<std::uint64_t>(state_.direction()) << 36U) |
        (static_cast<std::uint64_t>(state_.contract()) << 32U) |
        (static_cast<std::uint64_t>(state_.tricks_remaining()) << 24U) |
        (static_cast<std::uint64_t>(tricks_won_[0]) << 20U) |
        (static_cast<std::uint64_t>(tricks_won_[1]) << 16U) |
        (static_cast<std::uint64_t>(tricks_won_[2]) << 12U) |
        (static_cast<std::uint64_t>(tricks_won_[3]) << 8U) |
        (static_cast<std::uint64_t>(valid_reverse_count_ & 1U) << 7U) |
        (static_cast<std::uint64_t>(
            pending_block_.has_value()
                ? static_cast<std::uint8_t>(*pending_block_) : 7U) << 4U) |
        (state_.draw_index() & 0x0FU));
    for (const TrickPlay& play : trick) {
        std::uint64_t token =
            (static_cast<std::uint64_t>(play.seat) << 56U) |
            (static_cast<std::uint64_t>(play.action.is_pass() ? 1U : 0U) << 55U);
        if (!play.action.is_pass()) {
            const Card* card = state_.card(*play.action.card_id());
            if (card == nullptr || card->intrinsic_suits().size() != 1U) {
                return identity();
            }
            const std::size_t suit = suit_index(card->intrinsic_suits().front());
            const std::optional<std::uint8_t> bit =
                universe_bit(card->id(), static_cast<Suit>(suit));
            if (!bit.has_value()) {
                return identity();
            }
            const std::uint64_t above =
                live_mask_[suit] & ~((std::uint64_t{2}
                    << static_cast<std::uint8_t>(*bit)) - 1U);
            token |= (static_cast<std::uint64_t>(suit) << 48U) |
                (static_cast<std::uint64_t>(std::popcount(above)) << 32U) |
                (static_cast<std::uint64_t>(
                    static_cast<std::uint8_t>(*play.action.mode())) << 28U) |
                (static_cast<std::uint64_t>(
                    static_cast<std::uint8_t>(
                        *play.action.effective_suit())) << 24U);
        }
        mix(primary, verifier, token);
    }
    return PositionIdentity{primary, verifier};
}

auto Position::equivalent_actions(const Action& left, const Action& right) const
    -> bool
{
    if (!universe_.enabled || left.is_pass() || right.is_pass() ||
        left.mode() != right.mode() ||
        left.effective_suit() != right.effective_suit()) {
        return false;
    }
    const Card* left_card = state_.card(*left.card_id());
    const Card* right_card = state_.card(*right.card_id());
    if (left_card == nullptr || right_card == nullptr ||
        !is_plain_single_suit(*left_card) ||
        !is_plain_single_suit(*right_card) ||
        left_card->intrinsic_suits() != right_card->intrinsic_suits()) {
        return false;
    }
    const Suit suit = left_card->intrinsic_suits().front();
    const std::optional<std::uint8_t> left_bit = universe_bit(left_card->id(), suit);
    const std::optional<std::uint8_t> right_bit = universe_bit(right_card->id(), suit);
    if (!left_bit.has_value() || !right_bit.has_value()) {
        return false;
    }

    const auto rank_of = [](const Card& card) { return card.rank().millionths(); };
    const std::int32_t low = std::min(rank_of(*left_card), rank_of(*right_card));
    const std::int32_t high = std::max(rank_of(*left_card), rank_of(*right_card));
    const std::vector<std::pair<std::int32_t, std::uint8_t>>& ranks =
        universe_.ranks[suit_index(suit)];
    const auto first = std::lower_bound(
        ranks.begin(), ranks.end(), low,
        [](const std::pair<std::int32_t, std::uint8_t>& entry, const std::int32_t value) {
            return entry.first < value;
        });
    const auto last = std::upper_bound(
        ranks.begin(), ranks.end(), high,
        [](const std::int32_t value, const std::pair<std::int32_t, std::uint8_t>& entry) {
            return value < entry.first;
        });
    const std::size_t first_index =
        static_cast<std::size_t>(std::distance(ranks.begin(), first));
    const std::size_t last_index =
        static_cast<std::size_t>(std::distance(ranks.begin(), last));
    // A prefix mask holds the bits below each index, so XOR isolates
    // [first_index, last_index).
    const std::uint64_t range =
        universe_.prefix_masks[suit_index(suit)][last_index] ^
        universe_.prefix_masks[suit_index(suit)][first_index];
    const std::uint64_t excluded =
        (std::uint64_t{1} << *left_bit) | (std::uint64_t{1} << *right_bit);
    return (live_mask_[suit_index(suit)] & range & ~excluded) == 0;
}

auto Position::next_seat(const Seat seat) const -> Seat
{
    const int value = static_cast<int>(seat);
    return static_cast<Seat>(
        (value + (state_.direction() == Direction::Forward ? 1 : 3)) % 4
    );
}

auto Position::legal_actions() const -> ActionList
{
    ActionList actions;
    legal_actions_into(actions);
    return actions;
}

void Position::legal_actions_into(ActionList& out) const
{
    if (fast_active_) {
        legal_actions_fast(out);
        return;
    }
    if (state_.is_finished()) {
        out.clear();
        return;
    }
    if (pending_block_.has_value() && *pending_block_ == state_.next_player()) {
        out.clear();
        out.push_back(Action::pass());
        return;
    }
    touniu_solver::legal_actions_into(state_, current_trick_, out);
}

auto Position::apply(const Action& action) -> bool
{
    const ActionList legal = legal_actions();
    if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
        return false;
    }
    apply_known_legal(action);
    return true;
}

void Position::apply_known_legal(const Action& action)
{
    if (fast_active_) {
        apply_fast(action);
        return;
    }
    UndoRecord record;
    record.previous_identity = identity_;
    record.previous_live_mask = live_mask_;
    record.player = state_.next_player();
    record.previous_next_player = record.player;
    record.previous_direction = state_.direction();
    record.previous_tricks_remaining = state_.tricks_remaining();
    record.previous_draw_index = state_.draw_index();
    record.previous_pending_block = pending_block_;
    record.previous_valid_reverse_count = valid_reverse_count_;
    record.previous_tricks_won = tricks_won_;
    record.is_pass = action.is_pass();

    if (!record.is_pass) {
        record.card = *action.card_id();
        const std::vector<CardId>& hand = state_.hand(record.player);
        const auto found = std::find(hand.begin(), hand.end(), record.card);
        assert(found != hand.end());
        record.hand_index =
            static_cast<std::size_t>(std::distance(hand.begin(), found));
    }

    if (record.is_pass) {
        apply_pass(record);
    } else {
        apply_play(action, record);
    }

    record.completed_trick = current_trick_.empty();
    undo_stack_.push_back(std::move(record));
}

void Position::undo()
{
    if (fast_active_) {
        undo_fast();
        return;
    }
    assert(!undo_stack_.empty());
    const UndoRecord& record = undo_stack_.back();

    if (record.completed_trick) {
        // The completed trick was cleared by the action; restore the four
        // plays first, then drop this action's own play.
        current_trick_ = record.cleared_trick;
    }
    current_trick_.pop_back();

    for (auto drawn = record.drawn_cards.rbegin(); drawn != record.drawn_cards.rend();
         ++drawn) {
        state_.remove_from_hand(record.draw_target, *drawn);
        state_.remove_from_catalog(*drawn);
        if (universe_.enabled) {
            update_holdings(record.draw_target, *drawn, false);
        }
    }

    if (!record.is_pass) {
        state_.insert_into_hand(record.player, record.hand_index, record.card);
        if (universe_.enabled) {
            update_holdings(record.player, record.card, true);
        }
    }

    state_.set_draw_index(record.previous_draw_index);
    state_.set_tricks_remaining(record.previous_tricks_remaining);
    state_.set_direction(record.previous_direction);
    state_.set_next_player(record.previous_next_player);
    pending_block_ = record.previous_pending_block;
    valid_reverse_count_ = record.previous_valid_reverse_count;
    tricks_won_ = record.previous_tricks_won;
    live_mask_ = record.previous_live_mask;
    identity_ = record.previous_identity;

    undo_stack_.pop_back();
}

void Position::apply_pass(UndoRecord& record)
{
    const Seat player = state_.next_player();
    push_trick_play({player, Action::pass()});
    set_pending_block(std::nullopt);

    if (current_trick_.size() == 4U) {
        finish_trick(&record.cleared_trick);
    } else {
        set_next_player(next_seat(player));
    }
}

void Position::apply_play(const Action& action, UndoRecord& record)
{
    const Seat player = state_.next_player();
    const CardId card_id = *action.card_id();
    const Card* card = state_.card(card_id);
    assert(card != nullptr);
    const bool is_fourth = current_trick_.size() == 3U;
    const bool effect_valid = action.mode() != PlayMode::Discard;

    remove_hand_card(player, card_id);
    push_trick_play({player, action});

    if (is_effect_card(card->kind()) && effect_valid) {
        if (card->kind() == CardKind::Block && !is_fourth) {
            set_pending_block(next_seat(player));
        } else if (card->kind() == CardKind::Reverse) {
            set_valid_reverse_count(valid_reverse_count_ + 1U);
        } else if (card->kind() == CardKind::PlusTwo) {
            const Seat target = is_fourth ? current_trick_.front().seat : next_seat(player);
            record.draw_target = target;
            record.drawn_cards.push_back(draw_floating(target).id());
            record.drawn_cards.push_back(draw_floating(target).id());
        }
    }

    if (current_trick_.size() == 4U) {
        finish_trick(&record.cleared_trick);
    } else {
        set_next_player(next_seat(player));
    }
}

void Position::finish_trick(Trick* cleared_trick)
{
    if (cleared_trick != nullptr) {
        *cleared_trick = current_trick_;
    }
    retire_trick_cards(current_trick_);
    const TrickResult result = resolve_trick(state_, current_trick_);
    add_tricks_won(result.winner);
    if (state_.tricks_remaining() > 0) {
        set_tricks_remaining(state_.tricks_remaining() - 1U);
    }

    if (valid_reverse_count_ % 2U == 1U) {
        set_direction(
            state_.direction() == Direction::Forward
                ? Direction::Reverse : Direction::Forward
        );
    }

    set_next_player(result.winner);
    clear_trick();
    set_pending_block(std::nullopt);
    set_valid_reverse_count(0);
}

void Position::set_next_player(const Seat seat)
{
    const Seat previous = state_.next_player();
    if (previous == seat) {
        return;
    }
    if (fast_active_) {
        state_.set_next_player(seat);
        return;
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainNextPlayer,
        static_cast<std::uint64_t>(previous));
    identity_tokens::combine(
        identity_, identity_tokens::DomainNextPlayer,
        static_cast<std::uint64_t>(seat));
    state_.set_next_player(seat);
}

void Position::set_direction(const Direction direction)
{
    const Direction previous = state_.direction();
    if (previous == direction) {
        return;
    }
    if (fast_active_) {
        state_.set_direction(direction);
        return;
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainDirection,
        static_cast<std::uint64_t>(previous));
    identity_tokens::combine(
        identity_, identity_tokens::DomainDirection,
        static_cast<std::uint64_t>(direction));
    state_.set_direction(direction);
}

void Position::set_tricks_remaining(const std::uint32_t tricks_remaining)
{
    const std::uint32_t previous = state_.tricks_remaining();
    if (previous == tricks_remaining) {
        return;
    }
    if (fast_active_) {
        state_.set_tricks_remaining(tricks_remaining);
        return;
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainTricksRemaining, previous);
    identity_tokens::combine(
        identity_, identity_tokens::DomainTricksRemaining, tricks_remaining);
    state_.set_tricks_remaining(tricks_remaining);
}

void Position::set_pending_block(const std::optional<Seat> seat)
{
    if (pending_block_ == seat) {
        return;
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainPendingBlock,
        pending_block_.has_value()
            ? static_cast<std::uint64_t>(*pending_block_) + 1U : 0U);
    identity_tokens::combine(
        identity_, identity_tokens::DomainPendingBlock,
        seat.has_value() ? static_cast<std::uint64_t>(*seat) + 1U : 0U);
    pending_block_ = seat;
}

void Position::set_valid_reverse_count(const std::uint32_t count)
{
    if (valid_reverse_count_ == count) {
        return;
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainValidReverseCount, valid_reverse_count_);
    identity_tokens::combine(
        identity_, identity_tokens::DomainValidReverseCount, count);
    valid_reverse_count_ = count;
}

void Position::add_tricks_won(const Seat seat)
{
    const std::size_t index = seat_index(seat);
    const std::uint64_t seat_bits = static_cast<std::uint64_t>(index) << 32U;
    identity_tokens::combine(
        identity_, identity_tokens::DomainSeatWins, seat_bits | tricks_won_[index]);
    ++tricks_won_[index];
    identity_tokens::combine(
        identity_, identity_tokens::DomainSeatWins, seat_bits | tricks_won_[index]);
}

void Position::push_trick_play(const TrickPlay& play)
{
    const std::uint64_t previous_size = current_trick_.size();
    current_trick_.push_back(play);
    identity_tokens::combine(
        identity_, identity_tokens::DomainTrickSize, previous_size);
    identity_tokens::combine(
        identity_, identity_tokens::DomainTrickSize, previous_size + 1U);
    identity_tokens::combine_trick_play(identity_, previous_size, play);
}

void Position::clear_trick()
{
    if (current_trick_.empty()) {
        return;
    }
    for (std::uint64_t index = 0; index < current_trick_.size(); ++index) {
        // Replaying a slot token removes it, since components are XORed.
        identity_tokens::combine_trick_play(identity_, index, current_trick_[index]);
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainTrickSize, current_trick_.size());
    identity_tokens::combine(identity_, identity_tokens::DomainTrickSize, 0U);
    current_trick_.clear();
}

void Position::remove_hand_card(const Seat seat, const CardId id)
{
    const std::size_t index = seat_index(seat);
    const std::uint64_t seat_bits = static_cast<std::uint64_t>(index) << 32U;
    const std::size_t previous_size = state_.hand(seat).size();
    if (!state_.remove_from_hand(seat, id)) {
        return;
    }
    if (universe_.enabled) {
        update_holdings(seat, id, false);
    }
    identity_tokens::combine(
        identity_, identity_tokens::DomainHandSize, seat_bits | previous_size);
    identity_tokens::combine(
        identity_, identity_tokens::DomainHandSize, seat_bits | (previous_size - 1U));
    identity_tokens::combine_hand_card(identity_, index, id);
}

auto Position::draw_floating(const Seat seat) -> Card
{
    const std::size_t index = seat_index(seat);
    const std::uint64_t seat_bits = static_cast<std::uint64_t>(index) << 32U;
    const std::uint64_t previous_draw_index = state_.draw_index();
    const std::size_t previous_size = state_.hand(seat).size();
    const std::size_t previous_catalog = state_.cards().size();
    const Card card = state_.draw_floating(seat);
    if (universe_.enabled) {
        const Suit suit = card.intrinsic_suits().front();
        if (!universe_bit(card.id(), suit).has_value()) {
            // A card outside the predicted universe invalidates the bitmask
            // shortcut; the search falls back to the reference predicate.
            universe_.enabled = false;
            live_mask_ = {};
            holdings_ = {};
        } else {
            update_holdings(seat, card.id(), true);
        }
    }

    identity_tokens::combine(
        identity_, identity_tokens::DomainDrawIndex, previous_draw_index);
    identity_tokens::combine(
        identity_, identity_tokens::DomainDrawIndex, state_.draw_index());
    identity_tokens::combine(
        identity_, identity_tokens::DomainHandSize, seat_bits | previous_size);
    identity_tokens::combine(
        identity_, identity_tokens::DomainHandSize, seat_bits | (previous_size + 1U));
    identity_tokens::combine_hand_card(identity_, index, card.id());
    identity_tokens::combine(
        identity_, identity_tokens::DomainCatalogSize, previous_catalog);
    identity_tokens::combine(
        identity_, identity_tokens::DomainCatalogSize, previous_catalog + 1U);
    identity_tokens::combine_card(identity_, card);
    return card;
}

}  // namespace touniu_solver
