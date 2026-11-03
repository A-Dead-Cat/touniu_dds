#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <vector>

#include <touniu_solver/state.hpp>

namespace touniu_solver {

class Action {
public:
    // Storage-only value; every accessor reports "absent".
    Action() = default;

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

// Legal actions with inline storage so a search node does not allocate for the
// common case. The list is either fully inline or fully on the heap, which keeps
// begin()/end() contiguous; it never truncates a large multi-mode hand.
class ActionList {
public:
    static constexpr std::size_t InlineCapacity = 24;

    ActionList() = default;
    ActionList(std::initializer_list<Action> actions);

    void push_back(const Action& action);
    void clear();

    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;
    [[nodiscard]] auto data() -> Action*;
    [[nodiscard]] auto data() const -> const Action*;
    [[nodiscard]] auto begin() -> Action*;
    [[nodiscard]] auto begin() const -> const Action*;
    [[nodiscard]] auto end() -> Action*;
    [[nodiscard]] auto end() const -> const Action*;
    [[nodiscard]] auto operator[](std::size_t index) -> Action&;
    [[nodiscard]] auto operator[](std::size_t index) const -> const Action&;
    [[nodiscard]] auto front() const -> const Action&;
    [[nodiscard]] auto back() const -> const Action&;

    // Cold paths (tests, C ABI) may still want an owning vector.
    operator std::vector<Action>() const;

private:
    std::array<Action, InlineCapacity> inline_{};
    std::vector<Action> overflow_;
    std::size_t size_ = 0;
};

void legal_actions_into(const State& state, const Trick& trick, ActionList& out);
auto legal_actions(const State& state, const Trick& trick) -> ActionList;

}  // namespace touniu_solver
