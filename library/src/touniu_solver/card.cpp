#include "card.hpp"

#include <cassert>
#include <utility>

namespace touniu_solver {
namespace {

constexpr std::int32_t Million = 1'000'000;
constexpr std::int32_t LowestRank = Million;
constexpr std::int32_t HighestRank = 16 * Million;

auto is_normal_suit(const Suit suit) -> bool
{
    return suit != Suit::NoTrump;
}

}  // namespace

Rank::Rank(const std::int32_t millionths) : millionths_(millionths)
{
}

auto Rank::from_millionths(const std::int32_t millionths) -> std::optional<Rank>
{
    if (millionths < LowestRank || millionths > HighestRank) {
        return std::nullopt;
    }
    return Rank(millionths);
}

auto Rank::floating(const std::int32_t millionths) -> std::optional<Rank>
{
    if (millionths <= LowestRank || millionths >= 15 * Million ||
        millionths % Million == 0) {
        return std::nullopt;
    }
    return Rank(millionths);
}

auto Rank::integer(const std::int32_t value) -> Rank
{
    assert(value >= 1 && value <= 16);
    return Rank(value * Million);
}

auto Rank::millionths() const -> std::int32_t
{
    return millionths_;
}

Card::Card(
    const CardId id,
    const CardKind kind,
    const Rank rank,
    std::vector<Suit> intrinsic_suits)
    : id_(id),
      kind_(kind),
      rank_(rank),
      intrinsic_suits_(std::move(intrinsic_suits))
{
}

auto Card::normal(const CardId id, const Suit suit, const Rank rank) -> Card
{
    assert(is_normal_suit(suit));
    return Card(id, CardKind::Normal, rank, {suit});
}

auto Card::floating(const CardId id, const Suit suit, const Rank rank) -> Card
{
    assert(is_normal_suit(suit));
    return Card(id, CardKind::Floating, rank, {suit});
}

auto Card::four_suit(const CardId id, const Rank rank) -> Card
{
    return Card(
        id,
        CardKind::FourSuit,
        rank,
        {Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs}
    );
}

auto Card::three_suit_cat(const CardId id) -> Card
{
    return Card(id, CardKind::ThreeSuitCat, Rank::integer(15), {});
}

auto Card::big_joker(const CardId id) -> Card
{
    return Card(id, CardKind::BigJoker, Rank::integer(16), {});
}

auto Card::small_joker(const CardId id) -> Card
{
    return Card(id, CardKind::SmallJoker, Rank::integer(15), {});
}

auto Card::plus_two(const CardId id, const Suit suit) -> Card
{
    assert(is_normal_suit(suit));
    return Card(id, CardKind::PlusTwo, Rank::integer(15), {suit});
}

auto Card::no_trump(const CardId id, const Rank rank) -> Card
{
    return Card(id, CardKind::NoTrump, rank, {Suit::NoTrump});
}

auto Card::block(const CardId id, const Suit suit) -> Card
{
    assert(is_normal_suit(suit));
    return Card(id, CardKind::Block, Rank::integer(1), {suit});
}

auto Card::reverse(const CardId id, const Suit suit) -> Card
{
    assert(is_normal_suit(suit));
    return Card(id, CardKind::Reverse, Rank::integer(1), {suit});
}

auto Card::cherry_bomb(const CardId id, const Suit suit) -> Card
{
    assert(is_normal_suit(suit));
    return Card(id, CardKind::CherryBomb, *Rank::from_millionths(14'000'001), {suit});
}

auto Card::id() const -> CardId
{
    return id_;
}

auto Card::kind() const -> CardKind
{
    return kind_;
}

auto Card::rank() const -> Rank
{
    return rank_;
}

auto Card::intrinsic_suits() const -> const std::vector<Suit>&
{
    return intrinsic_suits_;
}

}  // namespace touniu_solver
