#include "position.hpp"

#include <algorithm>
#include <cassert>

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
}

auto Position::create(const Scenario& scenario) -> std::optional<Position>
{
    const auto state = State::create(scenario);
    if (!state.has_value()) {
        return std::nullopt;
    }
    return Position(*state);
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
        const std::vector<Action> legal = replay.legal_actions();
        if (std::find(legal.begin(), legal.end(), action) == legal.end() ||
            !replay.apply(action)) {
            return std::nullopt;
        }
    }
    if (!matches_live_position(replay, live)) {
        return std::nullopt;
    }
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

auto Position::next_seat(const Seat seat) const -> Seat
{
    const int value = static_cast<int>(seat);
    return static_cast<Seat>(
        (value + (state_.direction() == Direction::Forward ? 1 : 3)) % 4
    );
}

auto Position::legal_actions() const -> std::vector<Action>
{
    if (state_.is_finished()) {
        return {};
    }
    if (pending_block_.has_value() && *pending_block_ == state_.next_player()) {
        return {Action::pass()};
    }
    return touniu_solver::legal_actions(state_, current_trick_);
}

auto Position::apply(const Action& action) -> bool
{
    const std::vector<Action> legal = legal_actions();
    if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
        return false;
    }

    undo_stack_.push_back(Snapshot{
        state_,
        current_trick_,
        pending_block_,
        valid_reverse_count_,
        tricks_won_,
    });

    if (action.is_pass()) {
        apply_pass();
    } else {
        apply_play(action);
    }
    return true;
}

void Position::undo()
{
    assert(!undo_stack_.empty());
    Snapshot snapshot = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    state_ = std::move(snapshot.state);
    current_trick_ = std::move(snapshot.current_trick);
    pending_block_ = snapshot.pending_block;
    valid_reverse_count_ = snapshot.valid_reverse_count;
    tricks_won_ = snapshot.tricks_won;
}

void Position::apply_pass()
{
    const Seat player = state_.next_player();
    current_trick_.push_back({player, Action::pass()});
    pending_block_ = std::nullopt;

    if (current_trick_.size() == 4U) {
        finish_trick();
    } else {
        state_.set_next_player(next_seat(player));
    }
}

void Position::apply_play(const Action& action)
{
    const Seat player = state_.next_player();
    const CardId card_id = *action.card_id();
    const Card* card = state_.card(card_id);
    assert(card != nullptr);
    const bool is_fourth = current_trick_.size() == 3U;
    const bool effect_valid = action.mode() != PlayMode::Discard;

    state_.remove_from_hand(player, card_id);
    current_trick_.push_back({player, action});

    if (is_effect_card(card->kind()) && effect_valid) {
        if (card->kind() == CardKind::Block && !is_fourth) {
            pending_block_ = next_seat(player);
        } else if (card->kind() == CardKind::Reverse) {
            ++valid_reverse_count_;
        } else if (card->kind() == CardKind::PlusTwo) {
            const Seat target = is_fourth ? current_trick_.front().seat : next_seat(player);
            state_.draw_floating(target);
            state_.draw_floating(target);
        }
    }

    if (current_trick_.size() == 4U) {
        finish_trick();
    } else {
        state_.set_next_player(next_seat(player));
    }
}

void Position::finish_trick()
{
    const TrickResult result = resolve_trick(state_, current_trick_);
    ++tricks_won_[seat_index(result.winner)];
    state_.complete_trick();

    if (valid_reverse_count_ % 2U == 1U) {
        state_.set_direction(
            state_.direction() == Direction::Forward
                ? Direction::Reverse : Direction::Forward
        );
    }

    state_.set_next_player(result.winner);
    current_trick_.clear();
    pending_block_ = std::nullopt;
    valid_reverse_count_ = 0;
}

}  // namespace touniu_solver
