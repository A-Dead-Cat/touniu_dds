#include "position.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>

#include <touniu_solver/live_position.hpp>
#include <touniu_solver/trick_resolution.hpp>

namespace touniu_solver {
namespace {

auto seat_index(const Seat seat) -> std::size_t
{
    return static_cast<std::size_t>(seat);
}

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
    return current_trick_;
}

auto Position::hand(const Seat seat) const -> const std::vector<CardId>&
{
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
    input.hands = &state_.hands();
    input.tricks_won = &tricks_won_;
    input.catalog = &state_.cards();
    input.current_trick = &current_trick_;
    return input;
}

void Position::refresh_identity()
{
    identity_ = identity_tokens::build_identity(identity_input());
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

void Position::rebuild_universe()
{
    universe_ = EquivalenceUniverse{};
    live_mask_ = {};

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
    std::vector<std::uint64_t> trump_entries;
    const auto add_capable = [&entries, &trump_entries](const Card& card) {
        const auto rank = card.rank().millionths();
        const std::uint64_t id = card.id().value;
        if (is_plain_single_suit(card)) {
            entries[suit_index(card.intrinsic_suits().front())].push_back({rank, id});
            return;
        }
        if (card.kind() == CardKind::BigJoker || card.kind() == CardKind::SmallJoker) {
            trump_entries.push_back(id);
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
        trump_entries.push_back(id);
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

    std::sort(trump_entries.begin(), trump_entries.end());
    trump_entries.erase(
        std::unique(trump_entries.begin(), trump_entries.end()),
        trump_entries.end());
    if (trump_entries.size() > 64U) {
        universe_.trump_overflow = true;
    } else {
        universe_.trump_cards = trump_entries;
        for (std::size_t index = 0; index < trump_entries.size(); ++index) {
            UniverseCard* card = universe_card(trump_entries[index]);
            if (card != nullptr) {
                card->trump_bit = static_cast<std::uint8_t>(index);
            }
            universe_.masks[4] |= std::uint64_t{1} << static_cast<std::uint8_t>(index);
        }
    }

    live_mask_ = universe_.masks;
    universe_.enabled = true;
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

auto Position::leader_sure_tricks() const -> SureTricks
{
    SureTricks result;
    if (state_.contract() != Contract::NoTrump || !current_trick_.empty() ||
        pending_block_.has_value() || !universe_.enabled ||
        universe_.trump_overflow) {
        return result;
    }
    if (live_mask_[4] != 0) {
        return result;
    }
    const Seat leader = state_.next_player();
    result.north_south = leader == Seat::North || leader == Seat::South;
    const std::vector<CardId>& hand = state_.hand(leader);
    std::uint32_t count = 0;
    for (std::size_t suit = 0; suit < 4; ++suit) {
        const std::uint64_t live = live_mask_[suit];
        if (live == 0) {
            continue;
        }
        int bit = 63;
        while (((live >> bit) & 1U) == 0U) {
            --bit;
        }
        const std::uint64_t id = universe_.ids[suit][static_cast<std::size_t>(bit)];
        const Card* card = state_.card(CardId{id});
        if (card == nullptr || !is_plain_single_suit(*card)) {
            continue;
        }
        if (std::find(hand.begin(), hand.end(), CardId{id}) == hand.end()) {
            continue;
        }
        ++count;
    }
    result.tricks = std::min(count, state_.tricks_remaining());
    return result;
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
    }

    if (!record.is_pass) {
        state_.insert_into_hand(record.player, record.hand_index, record.card);
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
