#include "state.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace touniu_solver {
namespace {

constexpr std::uint64_t GeneratedCardIdBase = 0x8000000000000000ULL;
constexpr std::uint64_t SplitMixIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t SplitMixMultiplierOne = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t SplitMixMultiplierTwo = 0x94D049BB133111EBULL;
constexpr std::int32_t Million = 1'000'000;
constexpr std::uint64_t FloatingRankCount = 13'999'999ULL;

auto seat_index(const Seat seat) -> std::size_t
{
    return static_cast<std::size_t>(seat);
}

auto splitmix64(std::uint64_t value) -> std::uint64_t
{
    value += SplitMixIncrement;
    value = (value ^ (value >> 30U)) * SplitMixMultiplierOne;
    value = (value ^ (value >> 27U)) * SplitMixMultiplierTwo;
    return value ^ (value >> 31U);
}

auto normal_suit_from_index(const std::uint64_t value) -> Suit
{
    return static_cast<Suit>(value % 4U);
}

}  // namespace

State::State(
    std::array<std::vector<CardId>, 4> hands,
    std::vector<Card> cards,
    const Seat next_player,
    const Direction direction,
    const Contract contract,
    const std::uint32_t tricks_remaining,
    const std::uint32_t seed,
    const std::uint64_t draw_index)
    : hands_(std::move(hands)),
      cards_(std::move(cards)),
      next_player_(next_player),
      direction_(direction),
      contract_(contract),
      tricks_remaining_(tricks_remaining),
      seed_(seed),
      draw_index_(draw_index)
{
}

auto State::create(const Scenario& scenario) -> std::optional<State>
{
    std::array<std::vector<CardId>, 4> hands;
    std::vector<Card> cards;

    for (std::size_t seat = 0; seat < scenario.hands.size(); ++seat) {
        for (const Card& card : scenario.hands[seat]) {
            const auto duplicate = std::find_if(
                cards.begin(),
                cards.end(),
                [&card](const Card& existing) { return existing.id() == card.id(); }
            );
            if (duplicate != cards.end()) {
                return std::nullopt;
            }
            hands[seat].push_back(card.id());
            cards.push_back(card);
        }
    }

    return State(
        std::move(hands),
        std::move(cards),
        scenario.next_player,
        scenario.direction,
        scenario.contract,
        scenario.tricks_remaining,
        scenario.seed.value_or(DefaultGenerationSeed),
        0
    );
}

auto State::restore(
    std::array<std::vector<CardId>, 4> hands,
    std::vector<Card> cards,
    const Seat next_player,
    const Direction direction,
    const Contract contract,
    const std::uint32_t tricks_remaining,
    const std::uint32_t seed,
    const std::uint64_t draw_index) -> std::optional<State>
{
    return State(
        std::move(hands),
        std::move(cards),
        next_player,
        direction,
        contract,
        tricks_remaining,
        seed,
        draw_index
    );
}

auto State::hand(const Seat seat) const -> const std::vector<CardId>&
{
    return hands_[seat_index(seat)];
}

auto State::card(const CardId id) const -> const Card*
{
    const auto found = std::find_if(cards_.begin(), cards_.end(), [id](const Card& card) {
        return card.id() == id;
    });
    return found == cards_.end() ? nullptr : &*found;
}

auto State::next_player() const -> Seat
{
    return next_player_;
}

auto State::contract() const -> Contract
{
    return contract_;
}

auto State::direction() const -> Direction
{
    return direction_;
}

auto State::requires_temporary_pass() const -> bool
{
    return hand(next_player_).empty();
}

auto State::seed() const -> std::uint32_t
{
    return seed_;
}

auto State::draw_index() const -> std::uint64_t
{
    return draw_index_;
}

auto State::tricks_remaining() const -> std::uint32_t
{
    return tricks_remaining_;
}

auto State::is_finished() const -> bool
{
    return tricks_remaining_ == 0;
}

auto State::draw_floating(const Seat seat) -> Card
{
    while (true) {
        const std::uint64_t value = splitmix64(
            static_cast<std::uint64_t>(seed_) + draw_index_
        );
        const auto rank = Rank::floating(
            Million + 1 + static_cast<std::int32_t>(value % FloatingRankCount)
        );
        const CardId id{GeneratedCardIdBase + draw_index_};
        ++draw_index_;

        if (!rank.has_value() || card_id_exists(id)) {
            continue;
        }

        Card card = Card::floating(id, normal_suit_from_index(value >> 32U), *rank);
        if (floating_template_exists(card)) {
            continue;
        }

        cards_.push_back(card);
        hands_[seat_index(seat)].push_back(card.id());
        return card;
    }
}

void State::complete_trick()
{
    if (tricks_remaining_ > 0) {
        --tricks_remaining_;
    }
}

auto State::remove_from_hand(const Seat seat, const CardId id) -> bool
{
    std::vector<CardId>& hand = hands_[seat_index(seat)];
    const auto found = std::find(hand.begin(), hand.end(), id);
    if (found == hand.end()) {
        return false;
    }
    hand.erase(found);
    return true;
}

void State::set_next_player(const Seat seat)
{
    next_player_ = seat;
}

void State::set_direction(const Direction direction)
{
    direction_ = direction;
}

auto State::card_id_exists(const CardId id) const -> bool
{
    return std::any_of(cards_.begin(), cards_.end(), [id](const Card& card) {
        return card.id() == id;
    });
}

auto State::floating_template_exists(const Card& candidate) const -> bool
{
    return std::any_of(cards_.begin(), cards_.end(), [&candidate](const Card& existing) {
        return existing.kind() == CardKind::Floating &&
               existing.rank() == candidate.rank() &&
               existing.intrinsic_suits() == candidate.intrinsic_suits();
    });
}

}  // namespace touniu_solver
