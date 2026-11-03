#pragma once

#include <array>
#include <cstdint>
#include <optional>
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
    [[nodiscard]] auto card(CardId id) const -> const Card*;
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
    void set_next_player(Seat seat);
    void set_direction(Direction direction);

private:
    State(
        std::array<std::vector<CardId>, 4> hands,
        std::vector<Card> cards,
        Seat next_player,
        Direction direction,
        Contract contract,
        std::uint32_t tricks_remaining,
        std::uint32_t seed
    );

    [[nodiscard]] auto card_id_exists(CardId id) const -> bool;
    [[nodiscard]] auto floating_template_exists(const Card& card) const -> bool;

    std::array<std::vector<CardId>, 4> hands_;
    std::vector<Card> cards_;
    Seat next_player_;
    Direction direction_;
    Contract contract_;
    std::uint32_t tricks_remaining_;
    std::uint32_t seed_;
    std::uint64_t draw_index_ = 0;
};

}  // namespace touniu_solver
