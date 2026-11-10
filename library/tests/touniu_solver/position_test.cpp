#include <touniu_solver/position.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <cstdio>
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

TEST(PositionTest, ConvertsSettledUnequalPlainTailToCompactOperations)
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<int>(Seat::West)] = {
        normal_card(1, Suit::Hearts, 2),
    };
    scenario.hands[static_cast<int>(Seat::North)] = {
        normal_card(2, Suit::Hearts, 3),
    };
    scenario.hands[static_cast<int>(Seat::East)] = {
        normal_card(3, Suit::Hearts, 4),
    };
    scenario.hands[static_cast<int>(Seat::South)] = {
        plus_two_card(4, Suit::Hearts),
    };
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    EXPECT_FALSE(position->uses_compact_operations());

    const auto play = [&](const CardId id) {
        const auto actions = position->legal_actions();
        const auto found = std::find_if(
            actions.begin(), actions.end(), [id](const Action& action) {
                return action.card_id() == id;
            });
        ASSERT_NE(found, actions.end());
        position->apply_known_legal(*found);
    };

    play(CardId{1});
    play(CardId{2});
    play(CardId{3});
    EXPECT_FALSE(position->compact_plain_tail().has_value());
    play(CardId{4});

    ASSERT_TRUE(position->current_trick().empty());
    EXPECT_EQ(position->next_player(), Seat::South);
    EXPECT_EQ(position->hand_size(Seat::West), 2U);
    EXPECT_EQ(position->hand_size(Seat::North), 0U);
    EXPECT_EQ(position->hand_size(Seat::East), 0U);
    EXPECT_EQ(position->hand_size(Seat::South), 0U);

    auto compact = position->compact_plain_tail();
    ASSERT_TRUE(compact.has_value());
    EXPECT_TRUE(compact->uses_compact_operations());
    EXPECT_EQ(compact->next_player(), position->next_player());
    EXPECT_EQ(compact->direction(), position->direction());
    EXPECT_EQ(compact->tricks_remaining(), position->tricks_remaining());
    EXPECT_EQ(compact->draw_index(), position->draw_index());
    EXPECT_EQ(compact->hand(Seat::West), position->hand(Seat::West));
    const auto actions = compact->legal_actions();
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_TRUE(actions.front().is_pass());
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

TEST(PositionTest, EquivalenceUniverseAccountsForPossibleDraws)
{
    Scenario generator_scenario;
    generator_scenario.seed = 0;
    auto generator = *State::create(generator_scenario);
    const Card predicted = generator.draw_floating(Seat::North);
    const Suit suit = predicted.intrinsic_suits().front();
    const std::int32_t rank = predicted.rank().millionths();
    ASSERT_GT(rank, 1'000'002);
    ASSERT_LT(rank, 14'999'997);

    const auto scenario_for = [&](const std::int32_t low, const std::int32_t high) {
        Scenario scenario;
        scenario.next_player = Seat::West;
        scenario.contract = Contract::NoTrump;
        scenario.tricks_remaining = 1;
        scenario.seed = 0;
        scenario.hands[static_cast<int>(Seat::West)] = {
            Card::floating(CardId{1}, suit, *Rank::floating(low)),
            Card::floating(CardId{2}, suit, *Rank::floating(high)),
            Card::plus_two(CardId{3}, Suit::Hearts),
        };
        return scenario;
    };

    // The predicted card sits strictly between these two candidates, so they
    // are not interchangeable even though they are adjacent right now.
    auto straddling = Position::create(scenario_for(rank - 1, rank + 1));
    ASSERT_TRUE(straddling.has_value());
    EXPECT_TRUE(straddling->has_equivalence_universe());
    EXPECT_FALSE(straddling->equivalent_actions(
        Action::play(CardId{1}, PlayMode::Lead, suit),
        Action::play(CardId{2}, PlayMode::Lead, suit)));

    // A one-millionth gap can never contain another rank, so the pair stays
    // interchangeable while the same +2 is still in the hand.
    auto adjacent = Position::create(scenario_for(14'999'998, 14'999'999));
    ASSERT_TRUE(adjacent.has_value());
    EXPECT_TRUE(adjacent->equivalent_actions(
        Action::play(CardId{1}, PlayMode::Lead, suit),
        Action::play(CardId{2}, PlayMode::Lead, suit)));
}

TEST(PositionTest, LeaderSureTricksCountsTopCardsAndTheHighestTrump)
{
    const auto scenario = [](const Contract contract) {
        Scenario deal;
        deal.next_player = Seat::West;
        deal.contract = contract;
        deal.tricks_remaining = 2;
        deal.hands[static_cast<int>(Seat::West)] = {
            normal_card(1, Suit::Spades, 14),
            normal_card(2, Suit::Hearts, 14),
        };
        deal.hands[static_cast<int>(Seat::North)] = {
            normal_card(3, Suit::Spades, 13),
            normal_card(4, Suit::Hearts, 13),
        };
        deal.hands[static_cast<int>(Seat::East)] = {
            normal_card(5, Suit::Spades, 12),
            normal_card(6, Suit::Hearts, 12),
        };
        deal.hands[static_cast<int>(Seat::South)] = {
            normal_card(7, Suit::Spades, 11),
            normal_card(8, Suit::Hearts, 11),
        };
        return deal;
    };

    auto plain = Position::create(scenario(Contract::NoTrump));
    ASSERT_TRUE(plain.has_value());
    const Position::SureTricks plain_sure = plain->leader_sure_tricks();
    EXPECT_TRUE(plain_sure.north_south == false);
    EXPECT_EQ(plain_sure.tricks, 2U);

    // In a spade contract the spade ace is the trump trick and the heart ace
    // is safe because every opponent still holds a heart to follow.
    auto suit_contract = Position::create(scenario(Contract::Spades));
    ASSERT_TRUE(suit_contract.has_value());
    EXPECT_EQ(suit_contract->leader_sure_tricks().tricks, 2U);

    // The same ace is not sure once an opponent is void and can ruff.
    Scenario ruffed = scenario(Contract::Spades);
    ruffed.hands[static_cast<int>(Seat::North)] = {
        normal_card(3, Suit::Spades, 13),
        normal_card(4, Suit::Spades, 3),
    };
    auto ruffed_position = Position::create(ruffed);
    ASSERT_TRUE(ruffed_position.has_value());
    EXPECT_EQ(ruffed_position->leader_sure_tricks().tricks, 1U);

    // The leader holds the highest trump; a lower trump outside means ordinary
    // top cards are not sure, but the trump itself is.
    Scenario with_trumps = scenario(Contract::NoTrump);
    with_trumps.hands[static_cast<int>(Seat::West)] = {
        Card::big_joker(CardId{1}),
        normal_card(2, Suit::Spades, 14),
    };
    with_trumps.hands[static_cast<int>(Seat::North)] = {
        Card::small_joker(CardId{3}),
        normal_card(4, Suit::Spades, 13),
    };
    auto trumps_outside = Position::create(with_trumps);
    ASSERT_TRUE(trumps_outside.has_value());
    // Both opponents still hold a spade, so they must follow and cannot ruff;
    // the trump trick and the spade top are both safe.
    EXPECT_EQ(trumps_outside->leader_sure_tricks().tricks, 2U);

    // An opponent who is void in spades and holds a trump can ruff, so only
    // the trump trick remains sure.
    Scenario void_opponent = with_trumps;
    void_opponent.hands[static_cast<int>(Seat::North)] = {
        Card::small_joker(CardId{4}),
        normal_card(5, Suit::Hearts, 2),
    };
    void_opponent.hands[static_cast<int>(Seat::West)] = {
        Card::big_joker(CardId{1}),
        normal_card(2, Suit::Spades, 14),
    };
    void_opponent.hands[static_cast<int>(Seat::East)] = {
        normal_card(6, Suit::Spades, 12),
        normal_card(7, Suit::Hearts, 12),
    };
    void_opponent.hands[static_cast<int>(Seat::South)] = {
        normal_card(8, Suit::Spades, 11),
        normal_card(9, Suit::Hearts, 11),
    };
    auto ruffable = Position::create(void_opponent);
    ASSERT_TRUE(ruffable.has_value());
    EXPECT_EQ(ruffable->leader_sure_tricks().tricks, 1U);

    // A four-suit card is not a real spade: its holder is never obliged to
    // follow, so it cannot make the spade top safe.
    Scenario four_suit_only = void_opponent;
    four_suit_only.hands[static_cast<int>(Seat::North)] = {
        Card::small_joker(CardId{4}),
        Card::four_suit(CardId{5}, Rank::integer(5)),
    };
    auto four_suit_bound = Position::create(four_suit_only);
    ASSERT_TRUE(four_suit_bound.has_value());
    EXPECT_EQ(four_suit_bound->leader_sure_tricks().tricks, 1U);

    // All live trumps are in the leader's hand, so the spade top also counts.
    with_trumps.hands[static_cast<int>(Seat::West)] = {
        Card::big_joker(CardId{1}),
        Card::small_joker(CardId{2}),
        normal_card(3, Suit::Spades, 14),
    };
    with_trumps.hands[static_cast<int>(Seat::North)] = {
        normal_card(4, Suit::Spades, 13),
        normal_card(5, Suit::Hearts, 2),
    };
    with_trumps.hands[static_cast<int>(Seat::East)] = {
        normal_card(6, Suit::Spades, 12),
        normal_card(7, Suit::Hearts, 12),
    };
    with_trumps.hands[static_cast<int>(Seat::South)] = {
        normal_card(8, Suit::Spades, 11),
        normal_card(9, Suit::Hearts, 11),
    };
    auto own_trumps = Position::create(with_trumps);
    ASSERT_TRUE(own_trumps.has_value());
    EXPECT_EQ(own_trumps->leader_sure_tricks().tricks, 2U);

    // A partner's top card is a sure trick when both partners hold the suit.
    Scenario partner_top;
    partner_top.next_player = Seat::West;
    partner_top.contract = Contract::NoTrump;
    partner_top.tricks_remaining = 1;
    partner_top.hands[static_cast<int>(Seat::West)] = {
        normal_card(1, Suit::Spades, 2),
    };
    partner_top.hands[static_cast<int>(Seat::East)] = {
        normal_card(2, Suit::Spades, 14),
    };
    partner_top.hands[static_cast<int>(Seat::North)] = {
        normal_card(3, Suit::Spades, 13),
    };
    partner_top.hands[static_cast<int>(Seat::South)] = {
        normal_card(4, Suit::Spades, 3),
    };
    auto partner_top_position = Position::create(partner_top);
    ASSERT_TRUE(partner_top_position.has_value());
    EXPECT_EQ(partner_top_position->leader_sure_tricks().tricks, 1U);

    // The partner's only card is needed for the counted suit, so cashing the
    // trump first would force a discard that breaks it. Only one trick counts.
    Scenario conflict;
    conflict.next_player = Seat::West;
    conflict.contract = Contract::NoTrump;
    conflict.tricks_remaining = 2;
    conflict.hands[static_cast<int>(Seat::West)] = {
        Card::big_joker(CardId{1}),
        normal_card(2, Suit::Spades, 2),
    };
    conflict.hands[static_cast<int>(Seat::East)] = {
        normal_card(3, Suit::Spades, 14),
    };
    conflict.hands[static_cast<int>(Seat::North)] = {
        normal_card(4, Suit::Spades, 13),
    };
    conflict.hands[static_cast<int>(Seat::South)] = {
        normal_card(5, Suit::Spades, 3),
    };
    auto conflict_position = Position::create(conflict);
    ASSERT_TRUE(conflict_position.has_value());
    EXPECT_EQ(conflict_position->leader_sure_tricks().tricks, 1U);

    // Once the trick has started the bound no longer applies.
    ASSERT_TRUE(plain->apply(
        action_for(plain->legal_actions(), CardId{1}, PlayMode::Lead)));
    EXPECT_EQ(plain->leader_sure_tricks().tricks, 0U);
}

auto ladder_scenario() -> Scenario
{
    Scenario scenario;
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = 2;
    scenario.hands[static_cast<int>(Seat::West)] = {
        normal_card(1, Suit::Spades, 2),
        normal_card(2, Suit::Spades, 3),
    };
    scenario.hands[static_cast<int>(Seat::North)] = {
        normal_card(3, Suit::Spades, 4),
        normal_card(4, Suit::Spades, 5),
    };
    scenario.hands[static_cast<int>(Seat::East)] = {
        normal_card(5, Suit::Spades, 6),
        normal_card(6, Suit::Spades, 7),
    };
    scenario.hands[static_cast<int>(Seat::South)] = {
        normal_card(7, Suit::Spades, 8),
        normal_card(8, Suit::Spades, 9),
    };
    return scenario;
}

void play_single_trick(Position& position, const std::uint64_t west_lead)
{
    const std::vector<Action> lead = position.legal_actions();
    ASSERT_TRUE(position.apply(action_for(lead, CardId{west_lead}, PlayMode::Lead)));
    const std::vector<Action> first = position.legal_actions();
    ASSERT_TRUE(position.apply(action_for(first, CardId{3}, PlayMode::Follow)));
    const std::vector<Action> second = position.legal_actions();
    ASSERT_TRUE(position.apply(action_for(second, CardId{5}, PlayMode::Follow)));
    const std::vector<Action> third = position.legal_actions();
    ASSERT_TRUE(position.apply(action_for(third, CardId{7}, PlayMode::Follow)));
}

TEST(PositionTest, SuitContractSureTricksAccountForCatsAndTrumps)
{
    const auto build = [](const bool opponent_cat) {
        Scenario deal;
        deal.next_player = Seat::West;
        deal.contract = Contract::Spades;
        deal.tricks_remaining = 2;
        deal.hands[static_cast<int>(Seat::West)] = {
            normal_card(1, Suit::Spades, 14),
            normal_card(2, Suit::Hearts, 9),
        };
        deal.hands[static_cast<int>(Seat::North)] = {
            opponent_cat ? Card::three_suit_cat(CardId{3})
                         : normal_card(3, Suit::Hearts, 6),
            normal_card(4, Suit::Spades, 2),
        };
        deal.hands[static_cast<int>(Seat::East)] = {
            normal_card(5, Suit::Spades, 3),
            normal_card(6, Suit::Hearts, 3),
        };
        deal.hands[static_cast<int>(Seat::South)] = {
            normal_card(7, Suit::Hearts, 4),
            normal_card(8, Suit::Hearts, 5),
        };
        return deal;
    };

    auto clean = Position::create(build(false));
    ASSERT_TRUE(clean.has_value());
    // The heart nine is safe, and so is the spade ace.
    EXPECT_EQ(clean->leader_sure_tricks().tricks, 2U);

    // The cat can be played as either the trump suit or as hearts, so neither
    // top card is provably safe any more.
    auto cat = Position::create(build(true));
    ASSERT_TRUE(cat.has_value());
    EXPECT_EQ(cat->leader_sure_tricks().tricks, 0U);
}

TEST(PositionTest, CompactOperationsMatchTheGeneralPath)
{
    // Random plain deals, random legal plays: the compact bitmask path must
    // reproduce the general CardId path exactly, including identity, hand
    // order, trick contents and win counts, and must unwind cleanly.
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        std::vector<Card> deck;
        std::uint64_t id = 1;
        for (int suit = 0; suit < 4; ++suit) {
            for (int rank = 2; rank <= 14; ++rank) {
                deck.push_back(Card::normal(
                    CardId{id++}, static_cast<Suit>(suit), Rank::integer(rank)));
            }
        }
        std::uint32_t state = seed * 2654435761U + 11U;
        for (std::size_t index = deck.size(); index > 1; --index) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            std::swap(deck[index - 1], deck[state % index]);
        }
        Scenario scenario;
        scenario.contract = static_cast<Contract>(seed % 5U);
        scenario.next_player = static_cast<Seat>(seed % 4U);
        scenario.tricks_remaining = 4;
        for (std::size_t slot = 0; slot < 16; ++slot) {
            scenario.hands[slot % 4].push_back(deck[slot]);
        }

        auto compact = Position::create(scenario);
        ASSERT_TRUE(compact->uses_compact_operations()) << "seed " << seed;
        set_compact_operations_disabled_for_testing(true);
        auto general = Position::create(scenario);
        set_compact_operations_disabled_for_testing(false);
        ASSERT_TRUE(compact.has_value());
        ASSERT_TRUE(general.has_value());

        int applied = 0;
        for (int ply = 0; ply < 12; ++ply) {
            ASSERT_EQ(compact->is_finished(), general->is_finished())
                << "seed " << seed << " ply " << ply;
            if (compact->is_finished()) {
                break;
            }
            EXPECT_EQ(compact->next_player(), general->next_player());
            EXPECT_EQ(compact->tricks_remaining(), general->tricks_remaining());
            EXPECT_EQ(compact->current_trick().size(),
                      general->current_trick().size());
            for (int seat = 0; seat < 4; ++seat) {
                const std::vector<CardId> compact_hand =
                    compact->hand(static_cast<Seat>(seat));
                const std::vector<CardId> general_hand =
                    general->hand(static_cast<Seat>(seat));
                EXPECT_EQ(compact_hand, general_hand)
                    << "seed " << seed << " ply " << ply << " seat " << seat;
                EXPECT_EQ(
                    compact->tricks_won(static_cast<Seat>(seat)),
                    general->tricks_won(static_cast<Seat>(seat)));
            }
            const std::vector<Action> compact_actions = compact->legal_actions();
            const std::vector<Action> general_actions = general->legal_actions();
            ASSERT_EQ(compact_actions, general_actions)
                << "seed " << seed << " ply " << ply;
            const auto& identity = compact->identity();
            const PositionIdentity reference =
                recompute_position_identity(*compact);
            EXPECT_EQ(identity.primary, reference.primary)
                << "seed " << seed << " ply " << ply;
            EXPECT_EQ(identity.verifier, reference.verifier)
                << "seed " << seed << " ply " << ply;

            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            const std::size_t pick = state % compact_actions.size();
            ASSERT_TRUE(compact->apply(compact_actions[pick]));
            ASSERT_TRUE(general->apply(general_actions[pick]));
            ++applied;
        }

        // Unwind everything and compare the initial state again.
        for (int ply = 0; ply < applied; ++ply) {
            compact->undo();
            general->undo();
        }
        for (int seat = 0; seat < 4; ++seat) {
            const std::vector<CardId> compact_hand =
                compact->hand(static_cast<Seat>(seat));
            const std::vector<CardId> general_hand =
                general->hand(static_cast<Seat>(seat));
            EXPECT_EQ(compact_hand, general_hand) << "seed " << seed;
            EXPECT_EQ(
                compact->tricks_won(static_cast<Seat>(seat)),
                general->tricks_won(static_cast<Seat>(seat)));
        }
        EXPECT_EQ(compact->tricks_remaining(), general->tricks_remaining());
        EXPECT_EQ(compact->next_player(), general->next_player());
        const auto& rewound = compact->identity();
        const PositionIdentity rewound_reference =
            recompute_position_identity(*compact);
        EXPECT_EQ(rewound.primary, rewound_reference.primary);
        EXPECT_EQ(rewound.verifier, rewound_reference.verifier);
    }
}

TEST(PositionTest, CompactOperationsMatchGeneralPathWithJokers)
{
    for (const Contract contract : {Contract::Spades, Contract::NoTrump}) {
        Scenario scenario;
        scenario.contract = contract;
        scenario.next_player = Seat::West;
        scenario.tricks_remaining = 2;
        scenario.hands[static_cast<int>(Seat::West)] = {
            Card::small_joker(CardId{1}),
            normal_card(2, Suit::Hearts, 2),
        };
        scenario.hands[static_cast<int>(Seat::North)] = {
            normal_card(3, Suit::Spades, 2),
            normal_card(4, Suit::Hearts, 3),
        };
        scenario.hands[static_cast<int>(Seat::East)] = {
            Card::big_joker(CardId{5}),
            normal_card(6, Suit::Spades, 3),
        };
        scenario.hands[static_cast<int>(Seat::South)] = {
            normal_card(7, Suit::Spades, 4),
            normal_card(8, Suit::Hearts, 4),
        };

        auto compact = Position::create(scenario);
        set_compact_operations_disabled_for_testing(true);
        auto general = Position::create(scenario);
        set_compact_operations_disabled_for_testing(false);
        ASSERT_TRUE(compact.has_value());
        ASSERT_TRUE(general.has_value());
        ASSERT_TRUE(compact->uses_compact_operations());

        for (int ply = 0; ply < 8; ++ply) {
            const std::vector<Action> compact_actions = compact->legal_actions();
            const std::vector<Action> general_actions = general->legal_actions();
            ASSERT_EQ(compact_actions, general_actions)
                << "contract " << static_cast<int>(contract)
                << " ply " << ply;
            if (compact_actions.empty()) {
                break;
            }
            const std::size_t choice = ply == 0
                ? static_cast<std::size_t>(std::find_if(
                      compact_actions.begin(), compact_actions.end(),
                      [](const Action& action) {
                          return action.card_id() == CardId{1};
                      }) - compact_actions.begin())
                : 0U;
            ASSERT_LT(choice, compact_actions.size());
            ASSERT_TRUE(compact->apply(compact_actions[choice]));
            ASSERT_TRUE(general->apply(general_actions[choice]));
            const PositionIdentity compact_key = compact->table_identity();
            const PositionIdentity general_key = general->table_identity();
            EXPECT_EQ(compact_key.primary, general_key.primary);
            EXPECT_EQ(compact_key.verifier, general_key.verifier);
            EXPECT_EQ(compact->next_player(), general->next_player());
            EXPECT_EQ(compact->tricks_remaining(), general->tricks_remaining());
            for (int seat = 0; seat < 4; ++seat) {
                EXPECT_EQ(
                    compact->tricks_won(static_cast<Seat>(seat)),
                    general->tricks_won(static_cast<Seat>(seat)));
            }
        }
    }
}

TEST(PositionTest, CompactOperationsMatchGeneralPathWithNoTrumpSuitCards)
{
    for (const Contract contract : {Contract::Spades, Contract::NoTrump}) {
        Scenario scenario;
        scenario.contract = contract;
        scenario.next_player = Seat::West;
        scenario.tricks_remaining = 2;
        scenario.hands[static_cast<int>(Seat::West)] = {
            Card::no_trump(CardId{1}, Rank::integer(10)),
            normal_card(2, Suit::Hearts, 2),
        };
        scenario.hands[static_cast<int>(Seat::North)] = {
            Card::small_joker(CardId{3}),
            Card::no_trump(CardId{4}, Rank::integer(5)),
        };
        scenario.hands[static_cast<int>(Seat::East)] = {
            Card::big_joker(CardId{5}),
            normal_card(6, Suit::Spades, 3),
        };
        scenario.hands[static_cast<int>(Seat::South)] = {
            Card::no_trump(CardId{7}, Rank::integer(8)),
            normal_card(8, Suit::Hearts, 4),
        };

        auto compact = Position::create(scenario);
        set_compact_operations_disabled_for_testing(true);
        auto general = Position::create(scenario);
        set_compact_operations_disabled_for_testing(false);
        ASSERT_TRUE(compact.has_value());
        ASSERT_TRUE(general.has_value());
        ASSERT_TRUE(compact->uses_compact_operations());

        for (int ply = 0; ply < 8; ++ply) {
            const std::vector<Action> compact_actions = compact->legal_actions();
            const std::vector<Action> general_actions = general->legal_actions();
            ASSERT_EQ(compact_actions, general_actions)
                << "contract " << static_cast<int>(contract)
                << " ply " << ply;
            if (ply == 1 && contract == Contract::Spades) {
                ASSERT_EQ(compact_actions.size(), 1U);
                EXPECT_EQ(compact_actions.front().card_id(), CardId{4});
            }
            if (ply == 1 && contract == Contract::NoTrump) {
                ASSERT_EQ(compact_actions.size(), 2U);
                EXPECT_EQ(compact_actions[0].card_id(), CardId{3});
                EXPECT_EQ(compact_actions[1].card_id(), CardId{4});
            }
            if (compact_actions.empty()) {
                break;
            }
            const std::size_t choice = ply == 0
                ? static_cast<std::size_t>(std::find_if(
                      compact_actions.begin(), compact_actions.end(),
                      [](const Action& action) {
                          return action.card_id() == CardId{1};
                      }) - compact_actions.begin())
                : 0U;
            ASSERT_LT(choice, compact_actions.size());
            ASSERT_TRUE(compact->apply(compact_actions[choice]));
            ASSERT_TRUE(general->apply(general_actions[choice]));
            const PositionIdentity compact_key = compact->table_identity();
            const PositionIdentity general_key = general->table_identity();
            EXPECT_EQ(compact_key.primary, general_key.primary);
            EXPECT_EQ(compact_key.verifier, general_key.verifier);
            EXPECT_EQ(compact->next_player(), general->next_player());
            EXPECT_EQ(compact->tricks_remaining(), general->tricks_remaining());
            for (int seat = 0; seat < 4; ++seat) {
                EXPECT_EQ(
                    compact->tricks_won(static_cast<Seat>(seat)),
                    general->tricks_won(static_cast<Seat>(seat)));
            }
        }
    }
}

TEST(PositionTest, ShareableIdentityMergesEquivalentCardsAfterATrick)
{
    auto leading_two = Position::create(ladder_scenario());
    auto leading_three = Position::create(ladder_scenario());
    ASSERT_TRUE(leading_two.has_value());
    ASSERT_TRUE(leading_three.has_value());
    ASSERT_TRUE(leading_two->has_shareable_identity());

    play_single_trick(*leading_two, 1);
    play_single_trick(*leading_three, 2);

    const auto two_key = leading_two->table_identity();
    const auto three_key = leading_three->table_identity();
    EXPECT_EQ(two_key.primary, three_key.primary);
    EXPECT_EQ(two_key.verifier, three_key.verifier);
}

TEST(PositionTest, ShareableIdentitySeparatesOwnership)
{
    // Same ranks remain, but the five belongs to East and the seven to North.
    Scenario swapped = ladder_scenario();
    swapped.hands[static_cast<int>(Seat::North)][1] = normal_card(4, Suit::Spades, 7);
    swapped.hands[static_cast<int>(Seat::East)][1] = normal_card(6, Suit::Spades, 5);
    auto straight = Position::create(ladder_scenario());
    auto mixed = Position::create(swapped);
    ASSERT_TRUE(straight.has_value());
    ASSERT_TRUE(mixed.has_value());

    play_single_trick(*straight, 1);
    const std::vector<Action> lead = mixed->legal_actions();
    ASSERT_TRUE(mixed->apply(action_for(lead, CardId{1}, PlayMode::Lead)));
    const std::vector<Action> first = mixed->legal_actions();
    ASSERT_TRUE(mixed->apply(action_for(first, CardId{3}, PlayMode::Follow)));
    const std::vector<Action> second = mixed->legal_actions();
    ASSERT_TRUE(mixed->apply(action_for(second, CardId{5}, PlayMode::Follow)));
    const std::vector<Action> third = mixed->legal_actions();
    ASSERT_TRUE(mixed->apply(action_for(third, CardId{7}, PlayMode::Follow)));

    const auto straight_key = straight->table_identity();
    const auto mixed_key = mixed->table_identity();
    EXPECT_FALSE(
        straight_key.primary == mixed_key.primary &&
        straight_key.verifier == mixed_key.verifier);
}

TEST(PositionTest, ShareableIdentitySeparatesJokerOwnership)
{
    Scenario west_joker;
    west_joker.contract = Contract::NoTrump;
    west_joker.next_player = Seat::West;
    west_joker.tricks_remaining = 1;
    west_joker.hands[static_cast<int>(Seat::West)] = {
        Card::big_joker(CardId{1}),
    };
    west_joker.hands[static_cast<int>(Seat::North)] = {
        normal_card(2, Suit::Spades, 2),
    };
    Scenario north_joker = west_joker;
    north_joker.hands[static_cast<int>(Seat::West)].clear();
    north_joker.hands[static_cast<int>(Seat::North)].insert(
        north_joker.hands[static_cast<int>(Seat::North)].begin(),
        Card::big_joker(CardId{1}));

    auto west = Position::create(west_joker);
    auto north = Position::create(north_joker);
    ASSERT_TRUE(west.has_value());
    ASSERT_TRUE(north.has_value());
    ASSERT_TRUE(west->has_shareable_identity());
    ASSERT_TRUE(north->has_shareable_identity());

    const PositionIdentity west_key = west->table_identity();
    const PositionIdentity north_key = north->table_identity();
    EXPECT_FALSE(
        west_key.primary == north_key.primary &&
        west_key.verifier == north_key.verifier);
}

TEST(PositionTest, ShareableIdentityIsUnavailableForEffectCards)
{
    Scenario scenario = ladder_scenario();
    scenario.hands[static_cast<int>(Seat::South)].push_back(
        Card::plus_two(CardId{9}, Suit::Spades));
    scenario.tricks_remaining = 2;
    auto position = Position::create(scenario);
    ASSERT_TRUE(position.has_value());
    EXPECT_FALSE(position->has_shareable_identity());
    // The fallback key must still be the exact incremental identity.
    const auto key = position->table_identity();
    EXPECT_EQ(key.primary, position->identity().primary);
    EXPECT_EQ(key.verifier, position->identity().verifier);
}

}  // namespace
}  // namespace touniu_solver
