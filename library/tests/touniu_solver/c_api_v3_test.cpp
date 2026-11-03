#include <touniu_solver/c_api.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct BatchFixture {
    std::vector<TouniuLiveCard> cards;
    std::vector<std::uint64_t> hand_ids;
    std::array<std::uint32_t, 4> hand_counts{};
    std::array<std::uint32_t, 4> tricks_won{};
    std::vector<TouniuAnalyzeRequest> requests;

    void bind()
    {
        for (TouniuAnalyzeRequest& request : requests) {
            request.cards = cards.data();
            request.card_count = static_cast<std::uint32_t>(cards.size());
            request.hand_card_ids = hand_ids.data();
            request.hand_counts = hand_counts.data();
            request.current_trick = nullptr;
            request.current_trick_count = 0;
            request.tricks_won = tricks_won.data();
            request.has_pending_block = 0;
            request.pending_block_seat = -1;
            request.valid_reverse_count = 0;
            request.seed = 0;
            request.trick_start_draw_index = 0;
            request.draw_index = 0;
            request.generated_card_ids = nullptr;
            request.generated_card_id_count = 0;
            request.use_alpha_beta = 1;
        }
    }
};

constexpr std::uint32_t suit_bit(const TouniuSuit suit)
{
    return 1U << suit;
}

auto make_request(
    const std::int32_t next_player,
    const std::int32_t target) -> TouniuAnalyzeRequest
{
    TouniuAnalyzeRequest request{};
    request.abi_version = TOUNIU_SOLVER_ABI_VERSION_V2;
    request.next_player = next_player;
    request.direction = TOUNIU_DIRECTION_FORWARD;
    request.contract = TOUNIU_CONTRACT_NO_TRUMP;
    request.target_partnership = target;
    request.tricks_remaining = 2;
    return request;
}

// Declarer order North, South, East, West with their left-hand leaders.
auto contract_batch() -> BatchFixture
{
    BatchFixture fixture;
    for (std::uint64_t id = 1; id <= 8; ++id) {
        fixture.cards.push_back({
            id,
            TOUNIU_CARD_NORMAL,
            static_cast<std::int32_t>(15'000'000 - id * 1'000'000),
            suit_bit(TOUNIU_SUIT_SPADES),
        });
        fixture.hand_ids.push_back(id);
    }
    fixture.hand_counts = {2, 2, 2, 2};
    fixture.requests = {
        make_request(TOUNIU_SEAT_EAST, TOUNIU_PARTNERSHIP_NORTH_SOUTH),
        make_request(TOUNIU_SEAT_WEST, TOUNIU_PARTNERSHIP_NORTH_SOUTH),
        make_request(TOUNIU_SEAT_SOUTH, TOUNIU_PARTNERSHIP_EAST_WEST),
        make_request(TOUNIU_SEAT_NORTH, TOUNIU_PARTNERSHIP_EAST_WEST),
    };
    fixture.bind();
    return fixture;
}

auto individual_v2_result(TouniuAnalyzeRequest& request) -> std::int32_t
{
    std::uint32_t required = 0U;
    std::int32_t status = touniu_solver_analyze_actions_v2(
        &request, nullptr, 0U, &required);
    EXPECT_EQ(status, TOUNIU_SOLVER_BUFFER_TOO_SMALL);
    std::vector<TouniuAnalyzeAction> records(required);
    status = touniu_solver_analyze_actions_v2(
        &request, records.data(), required, &required);
    EXPECT_EQ(status, TOUNIU_SOLVER_OK);
    std::int32_t best = 0;
    for (const TouniuAnalyzeAction& record : records) {
        best = std::max(best, record.final_tricks);
    }
    return best;
}

TEST(TouniuContractBatchV3Test, MatchesIndependentV2Results)
{
    BatchFixture fixture = contract_batch();
    std::array<std::int32_t, 4> expected{};
    for (std::size_t index = 0; index < fixture.requests.size(); ++index) {
        expected[index] = individual_v2_result(fixture.requests[index]);
    }

    std::array<std::int32_t, 4> results{};
    std::uint32_t required = 0U;
    const std::int32_t status = touniu_solver_analyze_contract_batch_v3(
        fixture.requests.data(), 4U, results.data(), 4U, &required);
    EXPECT_EQ(status, TOUNIU_SOLVER_OK);
    EXPECT_EQ(required, 4U);
    EXPECT_EQ(results, expected);

    // The same deal viewed from both partnerships is complementary.
    EXPECT_EQ(results[0] + results[3], 2);
}

TEST(TouniuContractBatchV3Test, NegotiatesTheOutputBuffer)
{
    BatchFixture fixture = contract_batch();
    std::uint32_t required = 0U;
    std::int32_t status = touniu_solver_analyze_contract_batch_v3(
        fixture.requests.data(), 4U, nullptr, 0U, &required);
    EXPECT_EQ(status, TOUNIU_SOLVER_BUFFER_TOO_SMALL);
    EXPECT_EQ(required, 4U);

    std::array<std::int32_t, 3> small{};
    status = touniu_solver_analyze_contract_batch_v3(
        fixture.requests.data(), 4U, small.data(), 3U, &required);
    EXPECT_EQ(status, TOUNIU_SOLVER_BUFFER_TOO_SMALL);
    EXPECT_EQ(required, 4U);
    // Nothing is written when the capacity is short.
    EXPECT_EQ(small[0], 0);
}

TEST(TouniuContractBatchV3Test, AcceptsCanonicalSubsets)
{
    BatchFixture fixture = contract_batch();
    std::array<std::int32_t, 2> results{};
    std::uint32_t required = 0U;
    const std::int32_t status = touniu_solver_analyze_contract_batch_v3(
        fixture.requests.data(), 2U, results.data(), 2U, &required);
    EXPECT_EQ(status, TOUNIU_SOLVER_OK);
    EXPECT_EQ(required, 2U);
}

TEST(TouniuContractBatchV3Test, RejectsIncompatibleBatches)
{
    auto run = [](BatchFixture& fixture) {
        std::array<std::int32_t, 4> results{};
        std::uint32_t required = 0U;
        return touniu_solver_analyze_contract_batch_v3(
            fixture.requests.data(),
            static_cast<std::uint32_t>(fixture.requests.size()),
            results.data(),
            static_cast<std::uint32_t>(results.size()),
            &required);
    };

    BatchFixture duplicate = contract_batch();
    duplicate.requests.pop_back();
    duplicate.requests.push_back(duplicate.requests.front());
    EXPECT_EQ(run(duplicate), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture order = contract_batch();
    std::swap(order.requests[0], order.requests[1]);
    EXPECT_EQ(run(order), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture hands = contract_batch();
    // A different but internally valid deal: one card moves from North to West.
    std::array<std::uint32_t, 4> other_counts{3, 1, 2, 2};
    hands.requests[2].hand_counts = other_counts.data();
    EXPECT_EQ(run(hands), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture seed = contract_batch();
    seed.requests[2].seed = 7U;
    EXPECT_EQ(run(seed), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture trick = contract_batch();
    trick.requests[1].has_pending_block = 1U;
    trick.requests[1].pending_block_seat = TOUNIU_SEAT_EAST;
    EXPECT_EQ(run(trick), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture combination = contract_batch();
    combination.requests[0] =
        make_request(TOUNIU_SEAT_SOUTH, TOUNIU_PARTNERSHIP_NORTH_SOUTH);
    EXPECT_EQ(run(combination), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture wrong_target = contract_batch();
    wrong_target.requests[0] =
        make_request(TOUNIU_SEAT_EAST, TOUNIU_PARTNERSHIP_EAST_WEST);
    EXPECT_EQ(run(wrong_target), TOUNIU_SOLVER_INVALID_REQUEST);

    BatchFixture empty = contract_batch();
    empty.requests.clear();
    EXPECT_EQ(run(empty), TOUNIU_SOLVER_INVALID_REQUEST);
}

TEST(TouniuContractBatchV3Test, RejectsNullArguments)
{
    BatchFixture fixture = contract_batch();
    std::array<std::int32_t, 4> results{};
    EXPECT_EQ(
        touniu_solver_analyze_contract_batch_v3(
            nullptr, 4U, results.data(), 4U, nullptr),
        TOUNIU_SOLVER_NULL_ARGUMENT);
    std::uint32_t required = 0U;
    EXPECT_EQ(
        touniu_solver_analyze_contract_batch_v3(
            nullptr, 4U, results.data(), 4U, &required),
        TOUNIU_SOLVER_INVALID_REQUEST);
}

}  // namespace
