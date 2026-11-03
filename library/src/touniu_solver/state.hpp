#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <touniu_solver/card.hpp>

namespace touniu_solver {

inline constexpr std::uint32_t DefaultGenerationSeed = 591601503U;

struct Scenario {
    std::array<std::vector<Card>, 4> hands;
    Seat next_player = Seat::West;
    Direction direction = Direction::Forward;
    Contract contract = Contract::NoTrump;
    std::uint32_t tricks_remaining = 0;
    std::optional<std::uint32_t> seed;
};

class State {
public:
    static auto create(const Scenario& scenario) -> std::optional<State>;

    [[nodiscard]] auto hand(Seat seat) const -> const std::vector<CardId>&;
    [[nodiscard]] auto hands() const -> const std::array<std::vector<CardId>, 4>&;
    [[nodiscard]] auto card(CardId id) const -> const Card*;
    [[nodiscard]] auto cards() const -> const std::vector<Card>&;
    [[nodiscard]] auto next_player() const -> Seat;
    [[nodiscard]] auto contract() const -> Contract;
    [[nodiscard]] auto direction() const -> Direction;
    [[nodiscard]] auto requires_temporary_pass() const -> bool;
    [[nodiscard]] auto seed() const -> std::uint32_t;
    [[nodiscard]] auto draw_index() const -> std::uint64_t;
    [[nodiscard]] auto tricks_remaining() const -> std::uint32_t;
    [[nodiscard]] auto is_finished() const -> bool;

    auto draw_floating(Seat seat) -> Card;
    void complete_trick();
    auto remove_from_hand(Seat seat, CardId id) -> bool;
    void insert_into_hand(Seat seat, std::size_t index, CardId id);
    void remove_from_catalog(CardId id);
    void set_next_player(Seat seat);
    void set_direction(Direction direction);
    void set_tricks_remaining(std::uint32_t tricks_remaining);
    void set_draw_index(std::uint64_t draw_index);

    State(const State& other);
    auto operator=(const State& other) -> State&;
    State(State&&) noexcept = default;
    auto operator=(State&&) noexcept -> State& = default;

    static void reset_copy_count_for_testing();
    [[nodiscard]] static auto copy_count_for_testing() -> std::uint64_t;

    // Derived accelerator, never part of position semantics: the card index is
    // rebuilt from the catalog in debug/test checks only.
    [[nodiscard]] auto card_index_is_consistent_for_testing() const -> bool;

private:
    friend class Position;

    static constexpr std::uint64_t GeneratedCardIdBase = 0x8000000000000000ULL;
    static thread_local std::uint64_t copy_count_;

    State(
        std::array<std::vector<CardId>, 4> hands,
        std::vector<Card> cards,
        Seat next_player,
        Direction direction,
        Contract contract,
        std::uint32_t tricks_remaining,
        std::uint32_t seed,
        std::uint64_t draw_index
    );

    [[nodiscard]] auto card_id_exists(CardId id) const -> bool;
    [[nodiscard]] auto floating_template_exists(const Card& card) const -> bool;
    void rebuild_card_index();
    void insert_card_index(std::uint64_t id, std::size_t position);
    void erase_card_index(std::uint64_t id, std::size_t position);
    [[nodiscard]] auto card_position(CardId id) const -> std::size_t;

    std::array<std::vector<CardId>, 4> hands_;
    std::vector<Card> cards_;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> card_index_;
    Seat next_player_;
    Direction direction_;
    Contract contract_;
    std::uint32_t tricks_remaining_;
    std::uint32_t seed_;
    std::uint64_t draw_index_ = 0;
};

}  // namespace touniu_solver
