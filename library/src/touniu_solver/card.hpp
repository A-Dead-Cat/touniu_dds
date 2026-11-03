#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

namespace touniu_solver {

struct CardId {
    std::uint64_t value = 0;

    auto operator<=>(const CardId&) const = default;
};

enum class Seat {
    West,
    North,
    East,
    South,
};

enum class Direction {
    Forward,
    Reverse,
};

enum class Suit {
    Spades,
    Hearts,
    Diamonds,
    Clubs,
    NoTrump,
};

enum class Contract {
    Spades,
    Hearts,
    Diamonds,
    Clubs,
    NoTrump,
};

enum class CardKind {
    Normal,
    Floating,
    FourSuit,
    Block,
    Reverse,
    PlusTwo,
    NoTrump,
    BigJoker,
    SmallJoker,
    CherryBomb,
    ThreeSuitCat,
};

enum class PlayMode {
    Lead,
    Follow,
    Discard,
    Trump,
};

class Rank {
public:
    static auto from_millionths(std::int32_t millionths) -> std::optional<Rank>;
    static auto floating(std::int32_t millionths) -> std::optional<Rank>;
    static auto integer(std::int32_t value) -> Rank;

    [[nodiscard]] auto millionths() const -> std::int32_t;
    auto operator<=>(const Rank&) const = default;

private:
    explicit Rank(std::int32_t millionths);

    std::int32_t millionths_ = 0;
};

class Card {
public:
    static auto normal(CardId id, Suit suit, Rank rank) -> Card;
    static auto floating(CardId id, Suit suit, Rank rank) -> Card;
    static auto four_suit(CardId id, Rank rank) -> Card;
    static auto three_suit_cat(CardId id) -> Card;
    static auto big_joker(CardId id) -> Card;
    static auto small_joker(CardId id) -> Card;
    static auto plus_two(CardId id, Suit suit) -> Card;
    static auto block(CardId id, Suit suit) -> Card;
    static auto reverse(CardId id, Suit suit) -> Card;
    static auto cherry_bomb(CardId id, Suit suit) -> Card;

    [[nodiscard]] auto id() const -> CardId;
    [[nodiscard]] auto kind() const -> CardKind;
    [[nodiscard]] auto rank() const -> Rank;
    [[nodiscard]] auto intrinsic_suits() const -> const std::vector<Suit>&;

private:
    Card(CardId id, CardKind kind, Rank rank, std::vector<Suit> intrinsic_suits);

    CardId id_;
    CardKind kind_;
    Rank rank_;
    std::vector<Suit> intrinsic_suits_;
};

}  // namespace touniu_solver
