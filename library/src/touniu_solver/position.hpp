#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>
#include <touniu_solver/position_identity.hpp>
#include <touniu_solver/state.hpp>

namespace touniu_solver {

struct LivePosition;

// A complete, reversible game position for search. It owns the deal state, the
// in-progress trick, the pending block target and the effective reverse count,
// and applies/undoes legal actions exactly (including deterministic +2 draws).
class Position {
public:
    static auto create(const Scenario& scenario) -> std::optional<Position>;
    static auto create(const LivePosition& live) -> std::optional<Position>;

    [[nodiscard]] auto legal_actions() const -> ActionList;
    // Same actions written into a caller-owned buffer so search can reuse one
    // list per ply instead of allocating at every node.
    void legal_actions_into(ActionList& out) const;
    auto apply(const Action& action) -> bool;
    // Search-only entry: the caller must pass an action from the current
    // legal_actions() of this position. It skips the second legality pass but
    // performs exactly the same state changes as apply().
    void apply_known_legal(const Action& action);
    void undo();

    [[nodiscard]] auto next_player() const -> Seat;
    [[nodiscard]] auto direction() const -> Direction;
    [[nodiscard]] auto contract() const -> Contract;
    [[nodiscard]] auto tricks_remaining() const -> std::uint32_t;
    [[nodiscard]] auto tricks_won(Seat seat) const -> std::uint32_t;
    [[nodiscard]] auto is_finished() const -> bool;
    [[nodiscard]] auto current_trick() const -> const Trick&;
    [[nodiscard]] auto hand(Seat seat) const -> const std::vector<CardId>&;
    [[nodiscard]] auto card(CardId id) const -> const Card*;
    [[nodiscard]] auto card_catalog() const -> const std::vector<Card>&;
    [[nodiscard]] auto draw_index() const -> std::uint64_t;
    [[nodiscard]] auto seed() const -> std::uint32_t;
    [[nodiscard]] auto valid_reverse_count() const -> std::uint32_t;
    [[nodiscard]] auto pending_block() const -> std::optional<Seat>;

    // Identity maintained incrementally alongside every mutation. It always
    // equals recompute_position_identity(), which stays as the test oracle.
    [[nodiscard]] auto identity() const -> const PositionIdentity&;

    // Wide equivalence support. The deal universe is the live cards plus every
    // floating card this deal can still draw (the +2 count is fixed and the
    // draw sequence is play-order independent), so two plain cards of one suit
    // are interchangeable exactly when no live-or-possible card sits inside
    // their closed rank interval.
    [[nodiscard]] auto has_equivalence_universe() const -> bool;
    [[nodiscard]] auto equivalent_actions(
        const Action& left,
        const Action& right) const -> bool;

private:
    friend auto recompute_position_identity(const Position& position)
        -> PositionIdentity;
    // Only the data an action can change is recorded, so search can undo in
    // place instead of copying the whole deal state at every node.
    struct UndoRecord {
        bool is_pass = false;
        Seat player = Seat::West;
        CardId card{};
        std::size_t hand_index = 0;
        Seat previous_next_player = Seat::West;
        Direction previous_direction = Direction::Forward;
        std::uint32_t previous_tricks_remaining = 0;
        std::uint64_t previous_draw_index = 0;
        std::optional<Seat> previous_pending_block;
        std::uint32_t previous_valid_reverse_count = 0;
        std::array<std::uint32_t, 4> previous_tricks_won{};
        std::array<std::uint64_t, 4> previous_live_mask{};
        PositionIdentity previous_identity;
        Seat draw_target = Seat::West;
        std::vector<CardId> drawn_cards;
        bool completed_trick = false;
        Trick cleared_trick;
    };

    explicit Position(State state);

    [[nodiscard]] auto next_seat(Seat seat) const -> Seat;
    void apply_pass(UndoRecord& record);
    void apply_play(const Action& action, UndoRecord& record);
    void finish_trick(Trick* cleared_trick);

    void set_next_player(Seat seat);
    void set_direction(Direction direction);
    void set_tricks_remaining(std::uint32_t tricks_remaining);
    void set_pending_block(std::optional<Seat> seat);
    void set_valid_reverse_count(std::uint32_t count);
    void add_tricks_won(Seat seat);
    void push_trick_play(const TrickPlay& play);
    void clear_trick();
    void remove_hand_card(Seat seat, CardId id);
    auto draw_floating(Seat seat) -> Card;
    [[nodiscard]] auto identity_input() const -> identity_tokens::IdentityInput;
    void refresh_identity();

    struct UniverseCard {
        std::uint64_t id = 0;
        std::uint8_t suit_count = 0;
        std::array<std::uint8_t, 4> suits{};
        std::array<std::uint8_t, 4> bits{};
    };
    struct EquivalenceUniverse {
        bool enabled = false;
        std::array<std::vector<std::pair<std::int32_t, std::uint8_t>>, 4> ranks;
        std::array<std::vector<std::uint64_t>, 4> prefix_masks;
        std::array<std::uint64_t, 4> masks{};
        std::vector<UniverseCard> cards;
        std::vector<std::uint64_t> predicted_ids;
    };
    void rebuild_universe();
    void retire_trick_cards(const Trick& trick);
    void prune_reached_predictions();
    [[nodiscard]] auto universe_bit(CardId id, Suit suit) const
        -> std::optional<std::uint8_t>;
    [[nodiscard]] auto universe_card(std::uint64_t id) -> UniverseCard*;

    State state_;
    Trick current_trick_;
    std::optional<Seat> pending_block_;
    std::uint32_t valid_reverse_count_ = 0;
    std::array<std::uint32_t, 4> tricks_won_{};
    PositionIdentity identity_;
    EquivalenceUniverse universe_;
    std::array<std::uint64_t, 4> live_mask_{};
    std::vector<UndoRecord> undo_stack_;
};

// Reference identity recomputed from the complete position. The maintained
// Position::identity() must always equal it; tests use this as the oracle.
auto recompute_position_identity(const Position& position) -> PositionIdentity;

}  // namespace touniu_solver
