#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/legal_moves.hpp>

namespace touniu_solver {

struct LiveTrickPlay {
    Seat seat = Seat::West;
    bool is_pass = false;
    CardId card_id{};
    std::optional<PlayMode> mode;
    std::optional<Suit> effective_suit;
};

struct LivePosition {
    std::vector<Card> cards;
    std::array<std::vector<CardId>, 4> hands;
    std::vector<LiveTrickPlay> current_trick;
    Seat next_player = Seat::West;
    Direction direction = Direction::Forward;
    Contract contract = Contract::NoTrump;
    std::uint32_t tricks_remaining = 0;
    std::array<std::uint32_t, 4> tricks_won{};
    std::optional<Seat> pending_block;
    std::uint32_t valid_reverse_count = 0;
    std::uint32_t seed = DefaultGenerationSeed;
    std::uint64_t draw_index = 0;
};

auto validate_live_position(const LivePosition& live) -> bool;

}  // namespace touniu_solver
