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
    [[nodiscard]] auto hand_size(Seat seat) const -> std::uint32_t;
    // Rank lookup for the ordering heuristic: one open-addressed probe on the
    // compact path instead of a catalog search.
    [[nodiscard]] auto quick_rank(CardId id) const -> std::int32_t;
    [[nodiscard]] auto uses_compact_operations() const -> bool;
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

    // Lower-bound quick tricks for the side that is about to lead in a
    // no-trump position: an unbeatable plain top held by either partner, plus
    // the leader's highest trump, when the opponents must follow and cannot
    // ruff.
    struct SureTricks {
        bool north_south = false;
        std::uint32_t tricks = 0;
    };
    [[nodiscard]] auto leader_sure_tricks() const -> SureTricks;

    // Transposition key. For a deal whose deal universe is entirely ordinary
    // single-suit cards the key is built from the rank-compressed owner
    // sequence over the live cards, so positions that differ only by swapping
    // two interchangeable cards of one hand share an entry. Every other deal
    // falls back to the exact incremental identity.
    [[nodiscard]] auto table_identity() const -> PositionIdentity;
    [[nodiscard]] auto has_shareable_identity() const -> bool;

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
        std::array<std::uint64_t, 5> previous_live_mask{};
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
        std::uint8_t trump_bit = 0xFF;
    };
    struct EquivalenceUniverse {
        bool enabled = false;
        std::array<std::vector<std::pair<std::int32_t, std::uint8_t>>, 4> ranks;
        std::array<std::vector<std::uint64_t>, 4> ids;
        std::array<std::vector<std::uint64_t>, 4> prefix_masks;
        std::array<std::uint64_t, 5> masks{};
        std::vector<UniverseCard> cards;
        std::vector<std::uint64_t> predicted_ids;
        std::vector<std::uint64_t> trump_cards;
        // Rank of each trump-bucket bit, in bit order.
        std::vector<std::int32_t> trump_ranks;
        // Bucket-4 bits whose card is a joker or a NoTrump-kind card. Those are
        // trump in every contract; NoTrump-suit specials are trump only in a
        // no-trump contract, so they stay out of this mask.
        std::uint64_t nt_trump_mask = 0;
        // Bucket-4 bits whose card is a joker. Those are trump in every
        // contract, unlike NoTrump-kind cards, which are trump only in no-trump.
        std::uint64_t joker_mask = 0;
        bool trump_overflow = false;
    };
    void rebuild_universe();
    void update_holdings(Seat seat, CardId id, bool set);
    void retire_trick_cards(const Trick& trick);
    void prune_reached_predictions();
    // Compact operations for deals whose universe is only ordinary single-suit
    // cards. Hands live in bitmasks over the deal universe, so move generation
    // and make/undo perform no catalog lookups and no vector edits; the
    // CardId view is materialized on demand for the public API.
    struct FastUndo {
        bool pass = false;
        bool completed = false;
        std::uint8_t seat = 0;
        std::uint8_t bucket = 0;
        std::uint8_t bit = 0;
        Seat previous_next_player = Seat::West;
        std::uint32_t previous_tricks_remaining = 0;
        std::array<std::uint32_t, 4> previous_tricks_won{};
        std::array<std::uint64_t, 5> previous_live_mask{};
        std::array<TrickPlay, 4> previous_trick{};
        std::uint32_t previous_trick_size = 0;
    };
    void build_fast_state();
    auto materialize_hands() const -> const std::array<std::vector<CardId>, 4>&;
    void materialize_trick() const;
    [[nodiscard]] auto fast_slot(CardId id) const -> std::uint32_t;
    [[nodiscard]] auto fast_rank(std::uint8_t bucket, std::uint8_t bit) const
        -> std::int32_t;
    void legal_actions_fast(ActionList& out) const;
    void apply_fast(const Action& action);
    void undo_fast();
    void finish_trick_fast(FastUndo& record);
    [[nodiscard]] auto resolve_trick_fast() const -> Seat;

    [[nodiscard]] auto universe_bit(CardId id, Suit suit) const
        -> std::optional<std::uint8_t>;
    [[nodiscard]] auto universe_card(std::uint64_t id) -> UniverseCard*;

    State state_;
    mutable Trick current_trick_;
    std::optional<Seat> pending_block_;
    std::uint32_t valid_reverse_count_ = 0;
    std::array<std::uint32_t, 4> tricks_won_{};
    mutable PositionIdentity identity_;
    EquivalenceUniverse universe_;
    std::array<std::uint64_t, 5> live_mask_{};
    // Per-seat holdings over the same bit buckets: four suits plus trumps.
    std::array<std::array<std::uint64_t, 5>, 4> holdings_{};
    // Block cards still in hand, tracked because a block can force the
    // partner to pass on a trick it would otherwise win.
    std::array<std::uint32_t, 4> block_cards_{};
    // Four-suit and three-flower cards still in hand. They can be played as any
    // suit, so in a suit contract they can ruff a plain lead.
    std::array<std::uint32_t, 4> multi_suit_cards_{};
    bool fast_active_ = false;
    std::array<std::array<std::uint64_t, 5>, 4> fast_hands_{};
    std::array<std::uint32_t, 4> fast_sizes_{};
    std::array<TrickPlay, 4> fast_trick_{};
    std::uint32_t fast_trick_size_ = 0;
    std::vector<FastUndo> fast_undo_;
    // Original deal order as universe slots, so materialized hands reproduce
    // the same card order as the general path.
    std::array<std::array<std::uint32_t, 16>, 4> fast_order_{};
    std::array<std::uint32_t, 4> fast_order_count_{};
    // Open-addressed id -> (bucket << 8 | bit) index over the deal universe.
    std::array<std::uint32_t, 256> slot_index_{};
    mutable std::array<std::vector<CardId>, 4> hand_cache_;
    mutable bool hand_cache_valid_ = false;
    mutable bool identity_valid_ = true;
    // True when the deal universe is only ordinary single-suit cards, which is
    // what makes the rank-compressed table key sound.
    bool shareable_identity_ = false;
    // Owner of each universe bucket bit: seat index, or 0xFF when the card is
    // in the current trick or not in any hand.
    std::array<std::array<std::uint8_t, 64>, 5> owner_of_{};
    std::vector<UndoRecord> undo_stack_;
};

// Test hook: build positions with the general CardId operations even when the
// deal is plain, so the compact and the general paths can be compared.
void set_compact_operations_disabled_for_testing(bool disabled);

// Reference identity recomputed from the complete position. The maintained
// Position::identity() must always equal it; tests use this as the oracle.
auto recompute_position_identity(const Position& position) -> PositionIdentity;

}  // namespace touniu_solver
