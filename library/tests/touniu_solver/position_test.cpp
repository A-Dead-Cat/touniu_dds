#include <touniu_solver/position.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/live_position.hpp>
#include <touniu_solver/position_hash.hpp>
#include <touniu_solver/state.hpp>

namespace touniu_solver {
namespace {

auto normal_card(const std::uint64_t id, const Suit suit, const std::int32_t rank) -> Card
{
    return Card::normal(CardId{id}, suit, Rank::integer(rank));
}

auto plus_two_card(const std::uint64_t id, const Suit suit) -> Card
{
    return Card::plus_two(CardId{id}, suit);
}

auto reverse_card(const std::uint64_t id, const Suit suit) -> Card
{
    return Card::reverse(CardId{id}, suit);
}

auto block_card(const std::uint64_t id, const Suit suit) -> Card
{
    return Card::block(CardId{id}, suit);
}

auto action_for(const std::vector<Action>& actions, const CardId id, const PlayMode mode)
    -> Action
{
    for (const Action& action : actions) {
        if (action.card_id() == id && action.mode() == mode) {
            return action;
        }
    }
    return Action::pass();
}

TEST(PositionTest, ApplyPlayRemovesCardAndAdvancesPlayer)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {
        normal_card(1, Suit::Spades, 14),
        normal_card(2, Suit::Hearts, 3),
    };
    auto position = Position::create(scenario);

    ASSERT_TRUE(position.has_value());
    const auto actions = position->legal_actions();
    const Action lead = action_for(actions, CardId{1}, PlayMode::Lead);
    ASSERT_FALSE(lead.is_pass());

    ASSERT_TRUE(position->apply(lead));
    EXPECT_EQ(position->hand(Seat::West).size(), 1U);
    EXPECT_EQ(position->next_player(), Seat::North);

    position->undo();
    EXPECT_EQ(position->hand(Seat::West).size(), 2U);
    EXPECT_EQ(position->next_player(), Seat::West);
}

TEST(PositionTest, FullTrickCollectsAndUndoRestores)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {normal_card(2, Suit::Spades, 13)};
    scenario.hands[static_cast<int>(Seat::East)] = {normal_card(3, Suit::Spades, 12)};
    scenario.hands[static_cast<int>(Seat::South)] = {normal_card(4, Suit::Spades, 11)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const auto play = [&](const CardId id) {
        const auto actions = position->legal_actions();
        return position->apply(action_for(actions, id, PlayMode::Lead)) ||
               position->apply(action_for(actions, id, PlayMode::Follow));
    };

    ASSERT_TRUE(play(CardId{1}));
    ASSERT_TRUE(play(CardId{2}));
    ASSERT_TRUE(play(CardId{3}));
    ASSERT_TRUE(play(CardId{4}));

    EXPECT_TRUE(position->is_finished());
    EXPECT_EQ(position->tricks_remaining(), 0U);
    EXPECT_EQ(position->tricks_won(Seat::West), 1U);
    EXPECT_EQ(position->next_player(), Seat::West);
    EXPECT_TRUE(position->current_trick().empty());

    position->undo();
    EXPECT_FALSE(position->is_finished());
    EXPECT_EQ(position->tricks_remaining(), 1U);
    EXPECT_EQ(position->tricks_won(Seat::West), 0U);
    EXPECT_EQ(position->next_player(), Seat::South);
    EXPECT_EQ(position->current_trick().size(), 3U);
    EXPECT_EQ(position->hand(Seat::South).size(), 1U);
}

TEST(PositionTest, PlusTwoDrawsAndUndoRestoresDrawIndex)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {plus_two_card(1, Suit::Spades)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const auto actions = position->legal_actions();
    const Action lead = action_for(actions, CardId{1}, PlayMode::Lead);
    ASSERT_FALSE(lead.is_pass());
    ASSERT_TRUE(position->apply(lead));

    EXPECT_EQ(position->hand(Seat::North).size(), 2U);
    EXPECT_EQ(position->draw_index(), 2U);
    EXPECT_EQ(position->next_player(), Seat::North);

    position->undo();
    EXPECT_EQ(position->hand(Seat::North).size(), 0U);
    EXPECT_EQ(position->draw_index(), 0U);
    EXPECT_EQ(position->next_player(), Seat::West);
    EXPECT_EQ(position->hand(Seat::West).size(), 1U);
}

TEST(PositionTest, ReverseFlipsDirectionAtCollectionAndUndoRestores)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {normal_card(2, Suit::Spades, 13)};
    scenario.hands[static_cast<int>(Seat::East)] = {normal_card(3, Suit::Spades, 12)};
    scenario.hands[static_cast<int>(Seat::South)] = {reverse_card(4, Suit::Spades)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const auto play = [&](const CardId id) {
        const auto actions = position->legal_actions();
        const Action lead = action_for(actions, id, PlayMode::Lead);
        const Action follow = action_for(actions, id, PlayMode::Follow);
        return position->apply(!lead.is_pass() ? lead : follow);
    };

    ASSERT_TRUE(play(CardId{1}));
    ASSERT_TRUE(play(CardId{2}));
    ASSERT_TRUE(play(CardId{3}));
    ASSERT_TRUE(play(CardId{4}));

    EXPECT_EQ(position->direction(), Direction::Reverse);

    position->undo();
    EXPECT_EQ(position->direction(), Direction::Forward);
    EXPECT_EQ(position->valid_reverse_count(), 0U);
}

TEST(PositionTest, BlockForcesPassAndUndoRestores)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 14)};
    scenario.hands[static_cast<int>(Seat::North)] = {block_card(2, Suit::Spades)};
    scenario.hands[static_cast<int>(Seat::East)] = {};
    scenario.hands[static_cast<int>(Seat::South)] = {};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    ASSERT_TRUE(position->apply(action_for(position->legal_actions(), CardId{1}, PlayMode::Lead)));
    ASSERT_TRUE(position->apply(action_for(position->legal_actions(), CardId{2}, PlayMode::Follow)));

    const auto forced = position->legal_actions();
    ASSERT_EQ(forced.size(), 1U);
    EXPECT_TRUE(forced.front().is_pass());

    position->undo();
    const auto after_undo = position->legal_actions();
    EXPECT_FALSE(after_undo.size() == 1U && after_undo.front().is_pass());
    EXPECT_EQ(position->hand(Seat::North).size(), 1U);
}

TEST(PositionTest, ApplyAndUndoNeverCopyTheState)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<int>(Seat::West)] = {
        plus_two_card(1, Suit::Spades),
        normal_card(2, Suit::Hearts, 10),
    };
    scenario.hands[static_cast<int>(Seat::North)] = {reverse_card(3, Suit::Hearts)};
    scenario.hands[static_cast<int>(Seat::East)] = {normal_card(4, Suit::Hearts, 9)};
    scenario.hands[static_cast<int>(Seat::South)] = {normal_card(5, Suit::Hearts, 8)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    State::reset_copy_count_for_testing();
    std::size_t applied = 0;
    while (!position->is_finished()) {
        const std::vector<Action> actions = position->legal_actions();
        ASSERT_FALSE(actions.empty());
        ASSERT_TRUE(position->apply(actions.front()));
        ++applied;
    }
    for (std::size_t step = 0; step < applied; ++step) {
        position->undo();
    }

    EXPECT_EQ(State::copy_count_for_testing(), 0U);
    EXPECT_EQ(position->tricks_remaining(), 2U);
    EXPECT_EQ(position->next_player(), Seat::West);
}

struct TrickView {
    Seat seat;
    Action action;

    bool operator==(const TrickView&) const = default;
};

struct CardView {
    CardId id;
    CardKind kind;
    Rank rank;
    std::vector<Suit> suits;

    bool operator==(const CardView&) const = default;
};

struct ObservableState {
    Seat next_player = Seat::West;
    Direction direction = Direction::Forward;
    Contract contract = Contract::NoTrump;
    std::uint32_t tricks_remaining = 0;
    std::uint32_t seed = 0;
    std::uint64_t draw_index = 0;
    std::uint32_t valid_reverse_count = 0;
    std::optional<Seat> pending_block;
    std::array<std::uint32_t, 4> tricks_won{};
    std::array<std::vector<CardId>, 4> hands;
    std::vector<CardView> catalog;
    std::vector<TrickView> trick;
    PositionIdentity identity;

    bool operator==(const ObservableState&) const = default;
};

auto observe(const Position& position) -> ObservableState
{
    ObservableState observed;
    observed.next_player = position.next_player();
    observed.direction = position.direction();
    observed.contract = position.contract();
    observed.tricks_remaining = position.tricks_remaining();
    observed.seed = position.seed();
    observed.draw_index = position.draw_index();
    observed.valid_reverse_count = position.valid_reverse_count();
    observed.pending_block = position.pending_block();
    for (int seat = 0; seat < 4; ++seat) {
        const Seat value = static_cast<Seat>(seat);
        observed.tricks_won[static_cast<std::size_t>(seat)] = position.tricks_won(value);
        observed.hands[static_cast<std::size_t>(seat)] = position.hand(value);
    }
    for (const Card& card : position.card_catalog()) {
        observed.catalog.push_back(
            CardView{card.id(), card.kind(), card.rank(), card.intrinsic_suits()});
    }
    for (const TrickPlay& play : position.current_trick()) {
        observed.trick.push_back(TrickView{play.seat, play.action});
    }
    observed.identity = recompute_position_identity(position);
    return observed;
}

auto expect_identity_parity(const Position& position) -> void
{
    EXPECT_TRUE(position.identity() == recompute_position_identity(position));
}

auto mixed_scenario() -> Scenario
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::Spades;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<int>(Seat::West)] = {
        normal_card(1, Suit::Spades, 14),
        plus_two_card(2, Suit::Spades),
        normal_card(3, Suit::Hearts, 5),
        block_card(4, Suit::Spades),
    };
    scenario.hands[static_cast<int>(Seat::North)] = {
        normal_card(5, Suit::Spades, 13),
        reverse_card(6, Suit::Spades),
        normal_card(7, Suit::Hearts, 9),
        normal_card(8, Suit::Spades, 2),
    };
    scenario.hands[static_cast<int>(Seat::East)] = {
        normal_card(9, Suit::Spades, 12),
        normal_card(10, Suit::Clubs, 4),
        plus_two_card(11, Suit::Hearts),
        normal_card(12, Suit::Spades, 3),
    };
    scenario.hands[static_cast<int>(Seat::South)] = {
        normal_card(13, Suit::Spades, 11),
        normal_card(14, Suit::Diamonds, 7),
        reverse_card(15, Suit::Hearts),
        normal_card(16, Suit::Spades, 5),
    };
    return scenario;
}

void check_undo_tree(
    Position& position,
    const int depth,
    std::size_t& visited,
    bool& saw_floating)
{
    if (depth <= 0 || visited >= 400U) {
        return;
    }
    const std::vector<Action> actions = position.legal_actions();
    for (const Action& action : actions) {
        if (visited >= 400U) {
            return;
        }
        ++visited;
        const ObservableState before = observe(position);
        ASSERT_TRUE(position.apply(action));
        expect_identity_parity(position);
        for (const Card& card : position.card_catalog()) {
            if (card.kind() == CardKind::Floating) {
                saw_floating = true;
            }
        }
        check_undo_tree(position, depth - 1, visited, saw_floating);
        position.undo();
        expect_identity_parity(position);
        EXPECT_TRUE(observe(position) == before)
            << "restore mismatch after " << visited << " expanded actions";
    }
}

TEST(PositionTest, PlusTwoUndoRepeatsTheSameGeneratedCardsAfterSkippedCandidates)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {plus_two_card(1, Suit::Spades)};
    // A floating card already occupies generation slot 0, so the first +2 draw
    // has to skip it and consume a later index.
    const auto reserved_rank = Rank::floating(1'000'001);
    ASSERT_TRUE(reserved_rank.has_value());
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::floating(CardId{0x8000000000000000ULL}, Suit::Spades, *reserved_rank),
    };
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    const Action lead =
        action_for(position->legal_actions(), CardId{1}, PlayMode::Lead);
    ASSERT_FALSE(lead.is_pass());
    ASSERT_TRUE(position->apply(lead));

    const std::vector<CardId>& first_hand = position->hand(Seat::North);
    ASSERT_EQ(first_hand.size(), 3U);
    const std::vector<CardId> first_drawn(first_hand.begin() + 1, first_hand.end());
    std::vector<CardView> first_definitions;
    for (const CardId id : first_drawn) {
        const Card* card = position->card(id);
        ASSERT_NE(card, nullptr);
        first_definitions.push_back(
            CardView{card->id(), card->kind(), card->rank(), card->intrinsic_suits()});
    }
    const std::uint64_t first_draw_index = position->draw_index();
    ASSERT_GT(first_draw_index, 2U);

    position->undo();
    EXPECT_EQ(position->hand(Seat::North).size(), 1U);
    EXPECT_EQ(position->card_catalog().size(), 2U);
    EXPECT_EQ(position->draw_index(), 0U);

    ASSERT_TRUE(position->apply(lead));
    const std::vector<CardId>& second_hand = position->hand(Seat::North);
    ASSERT_EQ(second_hand.size(), 3U);
    const std::vector<CardId> second_drawn(second_hand.begin() + 1, second_hand.end());
    EXPECT_TRUE(second_drawn == first_drawn);
    EXPECT_EQ(position->draw_index(), first_draw_index);
    for (std::size_t index = 0; index < first_definitions.size(); ++index) {
        const Card* card = position->card(first_drawn[index]);
        ASSERT_NE(card, nullptr);
        EXPECT_TRUE(
            (CardView{card->id(), card->kind(), card->rank(), card->intrinsic_suits()}) ==
            first_definitions[index]);
    }
}

void check_known_legal_tree(
    Position& via_apply,
    Position& direct,
    const int depth,
    std::size_t& visited)
{
    if (depth <= 0 || visited >= 300U) {
        return;
    }
    const std::vector<Action> actions = via_apply.legal_actions();
    const std::vector<Action> known = direct.legal_actions();
    ASSERT_EQ(actions.size(), known.size());
    for (std::size_t index = 0; index < known.size(); ++index) {
        if (visited >= 300U) {
            return;
        }
        ++visited;
        const ObservableState before = observe(direct);
        ASSERT_TRUE(via_apply.apply(actions[index]));
        direct.apply_known_legal(known[index]);
        EXPECT_TRUE(observe(via_apply) == observe(direct))
            << "state mismatch at expanded action " << visited;
        check_known_legal_tree(via_apply, direct, depth - 1, visited);
        via_apply.undo();
        direct.undo();
        EXPECT_TRUE(observe(direct) == before)
            << "undo mismatch at expanded action " << visited;
    }
}

TEST(PositionTest, ApplyKnownLegalMatchesApplyForEveryAction)
{
    auto via_apply = Position::create(mixed_scenario());
    auto direct = Position::create(mixed_scenario());
    ASSERT_TRUE(via_apply.has_value());
    ASSERT_TRUE(direct.has_value());

    std::size_t visited = 0;
    check_known_legal_tree(*via_apply, *direct, 4, visited);
    EXPECT_GT(visited, 0U);
    EXPECT_TRUE(observe(*via_apply) == observe(*direct));
}

TEST(PositionTest, ApplyUndoRestoresObservableStateForEveryAction)
{
    auto position = Position::create(mixed_scenario());
    ASSERT_TRUE(position.has_value());
    const ObservableState initial = observe(*position);

    std::size_t visited = 0;
    bool saw_floating = false;
    check_undo_tree(*position, 4, visited, saw_floating);

    EXPECT_GT(visited, 0U);
    EXPECT_TRUE(saw_floating);
    EXPECT_TRUE(observe(*position) == initial);
}

TEST(PositionTest, RandomApplyUndoSequenceRestoresEveryStep)
{
    auto position = Position::create(mixed_scenario());
    ASSERT_TRUE(position.has_value());

    std::vector<ObservableState> snapshots;
    std::uint32_t random_state = 0x2545F491U;
    const auto next_random = [&random_state]() {
        random_state ^= random_state << 13U;
        random_state ^= random_state >> 17U;
        random_state ^= random_state << 5U;
        return random_state;
    };

    while (snapshots.size() < 12U && !position->is_finished()) {
        const std::vector<Action> actions = position->legal_actions();
        ASSERT_FALSE(actions.empty());
        snapshots.push_back(observe(*position));
        const Action& action = actions[next_random() % actions.size()];
        ASSERT_TRUE(position->apply(action));
        expect_identity_parity(*position);
    }
    ASSERT_GE(snapshots.size(), 4U);

    while (!snapshots.empty()) {
        position->undo();
        expect_identity_parity(*position);
        EXPECT_TRUE(observe(*position) == snapshots.back());
        snapshots.pop_back();
    }
}

TEST(PositionTest, MaintainedIdentityMatchesReferenceForImportedLivePosition)
{
    LivePosition live;
    live.cards = {
        normal_card(1, Suit::Spades, 14),
        normal_card(2, Suit::Spades, 13),
        normal_card(3, Suit::Hearts, 12),
        normal_card(4, Suit::Clubs, 11),
    };
    live.hands[static_cast<std::size_t>(Seat::East)] = {CardId{3}};
    live.hands[static_cast<std::size_t>(Seat::South)] = {CardId{4}};
    live.current_trick = {
        {Seat::West, false, CardId{1}, PlayMode::Lead, Suit::Spades},
        {Seat::North, false, CardId{2}, PlayMode::Follow, Suit::Spades},
    };
    live.next_player = Seat::East;
    live.contract = Contract::Spades;
    live.tricks_remaining = 3;
    live.tricks_won = {2, 1, 0, 4};

    auto position = Position::create(live);
    ASSERT_TRUE(position.has_value());
    expect_identity_parity(*position);

    const std::vector<Action> actions = position->legal_actions();
    ASSERT_FALSE(actions.empty());
    ASSERT_TRUE(position->apply(actions.front()));
    expect_identity_parity(*position);
    position->undo();
    expect_identity_parity(*position);
}

TEST(PositionTest, MaintainedIdentityMatchesReferenceAtConstruction)
{
    auto position = Position::create(mixed_scenario());
    ASSERT_TRUE(position.has_value());
    expect_identity_parity(*position);

    Scenario single;
    single.tricks_remaining = 1;
    single.next_player = Seat::South;
    single.direction = Direction::Reverse;
    single.contract = Contract::Hearts;
    single.seed = 42U;
    single.hands[static_cast<int>(Seat::South)] = {normal_card(7, Suit::Hearts, 10)};
    single.hands[static_cast<int>(Seat::West)] = {normal_card(8, Suit::Clubs, 2)};
    auto simple = Position::create(single);
    ASSERT_TRUE(simple.has_value());
    expect_identity_parity(*simple);
}

TEST(PositionTest, NoTrumpSuitPlusTwoDiscardsWithoutDrawingAndWinsAsTrump)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(Seat::West)] = {normal_card(1, Suit::Spades, 13)};
    scenario.hands[static_cast<int>(Seat::North)] = {
        Card::special(CardId{2}, CardKind::PlusTwo, {Suit::NoTrump}),
    };
    scenario.hands[static_cast<int>(Seat::East)] = {normal_card(3, Suit::Hearts, 5)};
    scenario.hands[static_cast<int>(Seat::South)] = {normal_card(4, Suit::Diamonds, 5)};
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());

    ASSERT_TRUE(position->apply(
        action_for(position->legal_actions(), CardId{1}, PlayMode::Lead)));
    const std::vector<Action> north_actions = position->legal_actions();
    ASSERT_EQ(north_actions.size(), 1U);
    EXPECT_EQ(
        north_actions.front(),
        Action::play(CardId{2}, PlayMode::Discard, Suit::NoTrump));

    const std::size_t east_before = position->hand(Seat::East).size();
    ASSERT_TRUE(position->apply(north_actions.front()));
    // An off-suit +2NT is not a valid effect, so nobody draws.
    EXPECT_EQ(position->hand(Seat::East).size(), east_before);

    while (!position->is_finished()) {
        const std::vector<Action> actions = position->legal_actions();
        ASSERT_FALSE(actions.empty());
        ASSERT_TRUE(position->apply(actions.front()));
    }
    EXPECT_EQ(position->tricks_won(Seat::North), 1U);
}

}  // namespace
}  // namespace touniu_solver
