#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>
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

    [[nodiscard]] auto legal_actions() const -> std::vector<Action>;
    auto apply(const Action& action) -> bool;
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

private:
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

    State state_;
    Trick current_trick_;
    std::optional<Seat> pending_block_;
    std::uint32_t valid_reverse_count_ = 0;
    std::array<std::uint32_t, 4> tricks_won_{};
    std::vector<UndoRecord> undo_stack_;
};

}  // namespace touniu_solver
