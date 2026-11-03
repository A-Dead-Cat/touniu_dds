#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>
#include <touniu_solver/state.hpp>

namespace touniu_solver {

// A complete, reversible game position for search. It owns the deal state, the
// in-progress trick, the pending block target and the effective reverse count,
// and applies/undoes legal actions exactly (including deterministic +2 draws).
class Position {
public:
    static auto create(const Scenario& scenario) -> std::optional<Position>;

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
    [[nodiscard]] auto draw_index() const -> std::uint64_t;
    [[nodiscard]] auto valid_reverse_count() const -> std::uint32_t;

private:
    struct Snapshot {
        State state;
        Trick current_trick;
        std::optional<Seat> pending_block;
        std::uint32_t valid_reverse_count;
        std::array<std::uint32_t, 4> tricks_won;
    };

    explicit Position(State state);

    [[nodiscard]] auto next_seat(Seat seat) const -> Seat;
    void apply_pass();
    void apply_play(const Action& action);
    void finish_trick();

    State state_;
    Trick current_trick_;
    std::optional<Seat> pending_block_;
    std::uint32_t valid_reverse_count_ = 0;
    std::array<std::uint32_t, 4> tricks_won_{};
    std::vector<Snapshot> undo_stack_;
};

}  // namespace touniu_solver
