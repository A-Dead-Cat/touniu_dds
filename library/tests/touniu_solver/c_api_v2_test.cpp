#include <touniu_solver/c_api.h>

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <touniu_solver/position.hpp>

namespace {

constexpr std::uint32_t suit_bit(const TouniuSuit suit)
{
    return 1U << suit;
}

struct Fixture {
    std::vector<TouniuLiveCard> cards;
    std::vector<std::uint64_t> hand_ids;
    std::array<std::uint32_t, 4> hand_counts{};
    std::vector<TouniuLiveTrickPlay> trick;
    std::array<std::uint32_t, 4> tricks_won{};
    TouniuAnalyzeRequest request{};

    void bind()
    {
        request.cards = cards.data();
        request.hand_card_ids = hand_ids.data();
        request.hand_counts = hand_counts.data();
        request.current_trick = trick.data();
        request.tricks_won = tricks_won.data();
    }
};

auto west_lead_position() -> Fixture
{
    Fixture fixture;
    fixture.cards = {
        {1, TOUNIU_CARD_NORMAL, 14'000'000, suit_bit(TOUNIU_SUIT_SPADES)},
        {2, TOUNIU_CARD_NORMAL, 14'000'000, suit_bit(TOUNIU_SUIT_HEARTS)},
    };
    fixture.hand_ids = {1, 2};
    fixture.hand_counts = {1, 0, 1, 0};
    fixture.request.abi_version = TOUNIU_SOLVER_ABI_VERSION_V2;
    fixture.request.card_count = static_cast<std::uint32_t>(fixture.cards.size());
    fixture.request.current_trick_count = 0;
    fixture.request.next_player = TOUNIU_SEAT_WEST;
    fixture.request.direction = TOUNIU_DIRECTION_FORWARD;
    fixture.request.contract = TOUNIU_CONTRACT_NO_TRUMP;
    fixture.request.target_partnership = TOUNIU_PARTNERSHIP_EAST_WEST;
    fixture.request.tricks_remaining = 1;
    fixture.request.has_pending_block = 0;
    fixture.request.pending_block_seat = -1;
    fixture.request.valid_reverse_count = 0;
    fixture.request.seed = 0;
    fixture.request.trick_start_draw_index = 0;
    fixture.request.draw_index = 0;
    fixture.request.generated_card_ids = nullptr;
    fixture.request.generated_card_id_count = 0;
    fixture.request.use_alpha_beta = 1;
    fixture.bind();
    return fixture;
}

auto leading_pass_position() -> Fixture
{
    Fixture fixture;
    fixture.cards = {
        {1, TOUNIU_CARD_NORMAL, 14'000'000, suit_bit(TOUNIU_SUIT_SPADES)},
        {2, TOUNIU_CARD_NORMAL, 13'000'000, suit_bit(TOUNIU_SUIT_SPADES)},
        {3, TOUNIU_CARD_NORMAL, 12'000'000, suit_bit(TOUNIU_SUIT_SPADES)},
    };
    fixture.hand_ids = {1, 2, 3};
    fixture.hand_counts = {0, 1, 1, 1};
    fixture.request.abi_version = TOUNIU_SOLVER_ABI_VERSION_V2;
    fixture.request.card_count = static_cast<std::uint32_t>(fixture.cards.size());
    fixture.request.current_trick_count = 0;
    fixture.request.next_player = TOUNIU_SEAT_WEST;
    fixture.request.direction = TOUNIU_DIRECTION_FORWARD;
    fixture.request.contract = TOUNIU_CONTRACT_NO_TRUMP;
    fixture.request.target_partnership = TOUNIU_PARTNERSHIP_NORTH_SOUTH;
    fixture.request.tricks_remaining = 1;
    fixture.request.has_pending_block = 0;
    fixture.request.pending_block_seat = -1;
    fixture.request.valid_reverse_count = 0;
    fixture.request.seed = 0;
    fixture.request.trick_start_draw_index = 0;
    fixture.request.draw_index = 0;
    fixture.request.generated_card_ids = nullptr;
    fixture.request.generated_card_id_count = 0;
    fixture.request.use_alpha_beta = 1;
    fixture.bind();
    return fixture;
}

TEST(CApiV2Test, V1AbiVersionRemainsUnchanged)
{
    EXPECT_EQ(TOUNIU_SOLVER_ABI_VERSION, 1U);
    EXPECT_EQ(TOUNIU_SOLVER_ABI_VERSION_V2, 2U);
    static_assert(std::is_standard_layout_v<TouniuCardInput>);
    static_assert(std::is_standard_layout_v<TouniuSolveRequest>);
    static_assert(std::is_standard_layout_v<TouniuSolveOutput>);
    static_assert(std::is_standard_layout_v<TouniuAnalyzeRequest>);
    static_assert(std::is_standard_layout_v<TouniuAnalyzeAction>);
}

TEST(CApiV2Test, ReportsRequiredCountWithoutWritingToNullBuffer)
{
    Fixture fixture = west_lead_position();

    std::uint32_t required_count = 0;
    const std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, nullptr, 0, &required_count);

    EXPECT_EQ(status, TOUNIU_SOLVER_BUFFER_TOO_SMALL);
    EXPECT_EQ(required_count, 1U);
}

TEST(CApiV2Test, UndersizedBufferWritesNothingAndReportsCount)
{
    Fixture fixture = west_lead_position();

    std::uint32_t required_count = 0;
    std::array<TouniuAnalyzeAction, 0> records{};
    const std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, records.data(), 0, &required_count);

    EXPECT_EQ(status, TOUNIU_SOLVER_BUFFER_TOO_SMALL);
    EXPECT_EQ(required_count, 1U);
}

TEST(CApiV2Test, WritesEachLegalActionWithItsFinalTricks)
{
    Fixture fixture = west_lead_position();

    std::uint32_t required_count = 0;
    std::array<TouniuAnalyzeAction, 4> records{};
    const std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, records.data(),
        static_cast<std::uint32_t>(records.size()), &required_count);

    EXPECT_EQ(status, TOUNIU_SOLVER_OK);
    EXPECT_EQ(required_count, 1U);
    EXPECT_EQ(records[0].is_pass, 0U);
    EXPECT_EQ(records[0].card_id, 1U);
    EXPECT_EQ(records[0].mode, TOUNIU_PLAY_LEAD);
    EXPECT_EQ(records[0].effective_suit, TOUNIU_SUIT_SPADES);
    EXPECT_EQ(records[0].final_tricks, 1);
}

TEST(CApiV2Test, LeadingEmptyHandProducesPassAction)
{
    Fixture fixture = leading_pass_position();

    std::uint32_t required_count = 0;
    std::array<TouniuAnalyzeAction, 4> records{};
    const std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, records.data(),
        static_cast<std::uint32_t>(records.size()), &required_count);

    EXPECT_EQ(status, TOUNIU_SOLVER_OK);
    EXPECT_EQ(required_count, 1U);
    EXPECT_EQ(records[0].is_pass, 1U);
    EXPECT_EQ(records[0].card_id, 0U);
    EXPECT_EQ(records[0].mode, TOUNIU_PLAY_NONE);
    EXPECT_EQ(records[0].effective_suit, TOUNIU_SUIT_NONE);
    EXPECT_EQ(records[0].final_tricks, 1);
}

TEST(CApiV2Test, RejectsUnsupportedVersion)
{
    Fixture fixture = west_lead_position();
    fixture.request.abi_version = TOUNIU_SOLVER_ABI_VERSION;

    std::uint32_t required_count = 0;
    std::array<TouniuAnalyzeAction, 4> records{};
    const std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, records.data(),
        static_cast<std::uint32_t>(records.size()), &required_count);

    EXPECT_EQ(status, TOUNIU_SOLVER_UNSUPPORTED_VERSION);
    EXPECT_EQ(required_count, 0U);
}

TEST(CApiV2Test, RejectsNullRequiredCount)
{
    Fixture fixture = west_lead_position();

    EXPECT_EQ(touniu_solver_analyze_actions_v2(&fixture.request, nullptr, 0, nullptr),
              TOUNIU_SOLVER_NULL_ARGUMENT);
}

TEST(CApiV2Test, RejectsInconsistentPosition)
{
    Fixture fixture = west_lead_position();
    // Card 1 is played by West in the current trick but also left in hand.
    fixture.trick = {
        {TOUNIU_SEAT_WEST, 0, 1, TOUNIU_PLAY_LEAD, TOUNIU_SUIT_SPADES},
    };
    fixture.request.current_trick_count =
        static_cast<std::uint32_t>(fixture.trick.size());
    fixture.bind();

    std::uint32_t required_count = 0;
    std::array<TouniuAnalyzeAction, 4> records{};
    const std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, records.data(),
        static_cast<std::uint32_t>(records.size()), &required_count);

    EXPECT_EQ(status, TOUNIU_SOLVER_INVALID_REQUEST);
}

TEST(TouniuSolverCApiV2Test, AcceptsNoTrumpSuitSpecialAndDiscardsIt)
{
    Fixture fixture;
    fixture.cards = {
        {1, TOUNIU_CARD_NORMAL, 13'000'000, suit_bit(TOUNIU_SUIT_SPADES)},
        {2, TOUNIU_CARD_PLUS_TWO, 15'000'000, suit_bit(TOUNIU_SUIT_NO_TRUMP)},
        {3, TOUNIU_CARD_NORMAL, 5'000'000, suit_bit(TOUNIU_SUIT_HEARTS)},
        {4, TOUNIU_CARD_NORMAL, 5'000'000, suit_bit(TOUNIU_SUIT_DIAMONDS)},
    };
    fixture.hand_ids = {2, 3, 4};
    fixture.hand_counts = {0, 1, 1, 1};
    fixture.trick = {
        {TOUNIU_SEAT_WEST, 0, 1, TOUNIU_PLAY_LEAD, TOUNIU_SUIT_SPADES},
    };
    fixture.request.abi_version = TOUNIU_SOLVER_ABI_VERSION_V2;
    fixture.request.card_count = 4;
    fixture.request.current_trick_count = 1;
    fixture.request.next_player = TOUNIU_SEAT_NORTH;
    fixture.request.direction = TOUNIU_DIRECTION_FORWARD;
    fixture.request.contract = TOUNIU_CONTRACT_NO_TRUMP;
    fixture.request.target_partnership = TOUNIU_PARTNERSHIP_NORTH_SOUTH;
    fixture.request.tricks_remaining = 1;
    fixture.request.has_pending_block = 0;
    fixture.request.pending_block_seat = -1;
    fixture.request.valid_reverse_count = 0;
    fixture.request.seed = 0;
    fixture.request.trick_start_draw_index = 0;
    fixture.request.draw_index = 0;
    fixture.request.generated_card_ids = nullptr;
    fixture.request.generated_card_id_count = 0;
    fixture.request.use_alpha_beta = 1;
    fixture.bind();

    std::uint32_t required = 0U;
    std::int32_t status = touniu_solver_analyze_actions_v2(
        &fixture.request, nullptr, 0U, &required);
    ASSERT_EQ(status, TOUNIU_SOLVER_BUFFER_TOO_SMALL);
    ASSERT_EQ(required, 1U);
    std::vector<TouniuAnalyzeAction> records(required);
    status = touniu_solver_analyze_actions_v2(
        &fixture.request, records.data(), required, &required);
    ASSERT_EQ(status, TOUNIU_SOLVER_OK);
    ASSERT_EQ(required, 1U);
    EXPECT_EQ(records[0].is_pass, 0);
    EXPECT_EQ(records[0].card_id, 2U);
    EXPECT_EQ(records[0].mode, TOUNIU_PLAY_DISCARD);
    EXPECT_EQ(records[0].effective_suit, TOUNIU_SUIT_NO_TRUMP);

    // The imported position must agree with the native construction.
    touniu_solver::Scenario scenario;
    scenario.next_player = touniu_solver::Seat::West;
    scenario.contract = touniu_solver::Contract::NoTrump;
    scenario.tricks_remaining = 1;
    scenario.hands[static_cast<int>(touniu_solver::Seat::West)] = {
        touniu_solver::Card::normal(
            touniu_solver::CardId{1}, touniu_solver::Suit::Spades,
            touniu_solver::Rank::integer(13)),
    };
    scenario.hands[static_cast<int>(touniu_solver::Seat::North)] = {
        touniu_solver::Card::special(
            touniu_solver::CardId{2}, touniu_solver::CardKind::PlusTwo,
            {touniu_solver::Suit::NoTrump}),
    };
    scenario.hands[static_cast<int>(touniu_solver::Seat::East)] = {
        touniu_solver::Card::normal(
            touniu_solver::CardId{3}, touniu_solver::Suit::Hearts,
            touniu_solver::Rank::integer(5)),
    };
    scenario.hands[static_cast<int>(touniu_solver::Seat::South)] = {
        touniu_solver::Card::normal(
            touniu_solver::CardId{4}, touniu_solver::Suit::Diamonds,
            touniu_solver::Rank::integer(5)),
    };
    auto native = touniu_solver::Position::create(scenario);
    ASSERT_TRUE(native.has_value());
    ASSERT_TRUE(native->apply(native->legal_actions().front()));
    const std::vector<touniu_solver::Action> native_actions =
        native->legal_actions();
    ASSERT_EQ(native_actions.size(), 1U);
    EXPECT_EQ(native_actions.front().mode(), touniu_solver::PlayMode::Discard);
    EXPECT_EQ(
        native_actions.front().effective_suit(),
        touniu_solver::Suit::NoTrump);
}

}  // namespace
