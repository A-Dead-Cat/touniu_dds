#include "state.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace touniu_solver {
namespace {

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

thread_local std::uint64_t State::copy_count_ = 0;

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
    rebuild_card_index();
}

State::State(const State& other)
    : hands_(other.hands_),
      cards_(other.cards_),
      card_index_(other.card_index_),
      next_player_(other.next_player_),
      direction_(other.direction_),
      contract_(other.contract_),
      tricks_remaining_(other.tricks_remaining_),
      seed_(other.seed_),
      draw_index_(other.draw_index_)
{
    ++copy_count_;
}

auto State::operator=(const State& other) -> State&
{
    if (this != &other) {
        hands_ = other.hands_;
        cards_ = other.cards_;
        card_index_ = other.card_index_;
        next_player_ = other.next_player_;
        direction_ = other.direction_;
        contract_ = other.contract_;
        tricks_remaining_ = other.tricks_remaining_;
        seed_ = other.seed_;
        draw_index_ = other.draw_index_;
        ++copy_count_;
    }
    return *this;
}

void State::reset_copy_count_for_testing()
{
    copy_count_ = 0;
}

auto State::copy_count_for_testing() -> std::uint64_t
{
    return copy_count_;
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

auto State::hand(const Seat seat) const -> const std::vector<CardId>&
{
    return hands_[seat_index(seat)];
}

auto State::hands() const -> const std::array<std::vector<CardId>, 4>&
{
    return hands_;
}

auto State::card(const CardId id) const -> const Card*
{
    const std::size_t position = card_position(id);
    return position == cards_.size() ? nullptr : &cards_[position];
}

auto State::card_position(const CardId id) const -> std::size_t
{
    const auto found = std::lower_bound(
        card_index_.begin(),
        card_index_.end(),
        id.value,
        [](const std::pair<std::uint64_t, std::uint32_t>& entry, const std::uint64_t value) {
            return entry.first < value;
        });
    if (found == card_index_.end() || found->first != id.value) {
        return cards_.size();
    }
    return found->second;
}

auto State::cards() const -> const std::vector<Card>&
{
    return cards_;
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
        insert_card_index(card.id().value, cards_.size() - 1U);
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

void State::insert_into_hand(const Seat seat, const std::size_t index, const CardId id)
{
    std::vector<CardId>& hand = hands_[seat_index(seat)];
    const auto position = hand.begin() + static_cast<std::ptrdiff_t>(std::min(index, hand.size()));
    hand.insert(position, id);
}

void State::remove_from_catalog(const CardId id)
{
    const std::size_t position = card_position(id);
    if (position == cards_.size()) {
        return;
    }
    cards_.erase(cards_.begin() + static_cast<std::ptrdiff_t>(position));
    erase_card_index(id.value, position);
}

void State::set_tricks_remaining(const std::uint32_t tricks_remaining)
{
    tricks_remaining_ = tricks_remaining;
}

void State::set_draw_index(const std::uint64_t draw_index)
{
    draw_index_ = draw_index;
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
    return card_position(id) != cards_.size();
}

void State::rebuild_card_index()
{
    card_index_.clear();
    card_index_.reserve(cards_.size());
    for (std::size_t index = 0; index < cards_.size(); ++index) {
        card_index_.push_back(
            {cards_[index].id().value, static_cast<std::uint32_t>(index)});
    }
    std::sort(card_index_.begin(), card_index_.end());
}

void State::insert_card_index(const std::uint64_t id, const std::size_t position)
{
    const auto found = std::lower_bound(
        card_index_.begin(),
        card_index_.end(),
        id,
        [](const std::pair<std::uint64_t, std::uint32_t>& entry, const std::uint64_t value) {
            return entry.first < value;
        });
    card_index_.insert(
        found, {id, static_cast<std::uint32_t>(position)});
}

void State::erase_card_index(const std::uint64_t id, const std::size_t position)
{
    const auto found = std::lower_bound(
        card_index_.begin(),
        card_index_.end(),
        id,
        [](const std::pair<std::uint64_t, std::uint32_t>& entry, const std::uint64_t value) {
            return entry.first < value;
        });
    if (found != card_index_.end() && found->first == id) {
        card_index_.erase(found);
    }
    const auto erased = static_cast<std::uint32_t>(position);
    for (auto& entry : card_index_) {
        if (entry.second > erased) {
            --entry.second;
        }
    }
}

auto State::card_index_is_consistent_for_testing() const -> bool
{
    if (card_index_.size() != cards_.size()) {
        return false;
    }
    std::vector<std::pair<std::uint64_t, std::uint32_t>> expected;
    expected.reserve(cards_.size());
    for (std::size_t index = 0; index < cards_.size(); ++index) {
        expected.push_back({cards_[index].id().value, static_cast<std::uint32_t>(index)});
    }
    std::sort(expected.begin(), expected.end());
    return expected == card_index_;
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
