#pragma once

#include <optional>
#include <vector>

#include <touniu_solver/state.hpp>

namespace touniu_solver {

class Action {
public:
    static auto play(CardId card_id, PlayMode mode, Suit effective_suit) -> Action;
    static auto pass() -> Action;

    [[nodiscard]] auto is_pass() const -> bool;
    [[nodiscard]] auto card_id() const -> std::optional<CardId>;
    [[nodiscard]] auto mode() const -> std::optional<PlayMode>;
    [[nodiscard]] auto effective_suit() const -> std::optional<Suit>;
    auto operator<=>(const Action&) const = default;

private:
    Action(
        std::optional<CardId> card_id,
        std::optional<PlayMode> mode,
        std::optional<Suit> effective_suit
    );

    std::optional<CardId> card_id_;
    std::optional<PlayMode> mode_;
    std::optional<Suit> effective_suit_;
};

struct TrickPlay {
    Seat seat;
    Action action;
};

using Trick = std::vector<TrickPlay>;

auto legal_actions(const State& state, const Trick& trick) -> std::vector<Action>;

}  // namespace touniu_solver
