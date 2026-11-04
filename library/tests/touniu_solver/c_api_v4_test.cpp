#include <touniu_solver/c_api.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace touniu_solver {
namespace {

struct LiveDeal {
    std::vector<TouniuLiveCard> cards;
    std::array<std::vector<std::uint64_t>, 4> hands;
    std::array<std::uint32_t, 4> tricks_won{0, 0, 0, 0};
};

// Two cards each, plain, no trump: whichever side leads takes the ace trick and
// the other side the remaining one, so every action has an exact value.
LiveDeal make_deal() {
    LiveDeal deal;
    const std::array<std::pair<int, int>, 8> ranks = {{
        {0, 14}, {1, 14}, {0, 13}, {1, 13}, {0, 12}, {1, 12}, {0, 11}, {1, 11},
    }};
    std::uint64_t id = 1;
    for (const auto& [suit, rank] : ranks) {
        TouniuLiveCard card{};
        card.id = id;
        card.kind = TOUNIU_CARD_NORMAL;
        card.rank_millionths = rank * 1'000'000;
        card.suit_mask = 1U << suit;
        deal.cards.push_back(card);
        deal.hands[static_cast<std::size_t>(id % 4)].push_back(id);
        ++id;
    }
    return deal;
}

std::vector<std::uint64_t> flatten(const LiveDeal& deal) {
    std::vector<std::uint64_t> ids;
    for (const auto& hand : deal.hands) {
        for (const std::uint64_t id : hand) ids.push_back(id);
    }
    return ids;
}

TouniuAnalyzeRequest make_request(
    const LiveDeal& deal,
    const std::vector<std::uint64_t>& hand_ids,
    const std::uint32_t abi_version,
    const std::int32_t target) {
    TouniuAnalyzeRequest request{};
    request.abi_version = abi_version;
    request.cards = deal.cards.data();
    request.card_count = static_cast<std::uint32_t>(deal.cards.size());
    request.hand_card_ids = hand_ids.data();
    request.hand_counts = nullptr;
    request.current_trick = nullptr;
    request.current_trick_count = 0;
    request.next_player = TOUNIU_SEAT_WEST;
    request.direction = TOUNIU_DIRECTION_FORWARD;
    request.contract = TOUNIU_CONTRACT_NO_TRUMP;
    request.target_partnership = target;
    request.tricks_remaining = 2;
    request.tricks_won = deal.tricks_won.data();
    request.has_pending_block = 0;
    request.pending_block_seat = -1;
    request.valid_reverse_count = 0;
    request.seed = 1;
    request.trick_start_draw_index = 0;
    request.draw_index = 0;
    request.generated_card_ids = nullptr;
    request.generated_card_id_count = 0;
    request.use_alpha_beta = 1;
    return request;
}

TEST(CapiV4Test, BestMoveMatchesThePerActionMaximum) {
    const LiveDeal deal = make_deal();
    std::array<std::uint32_t, 4> hand_counts{};
    for (std::size_t seat = 0; seat < 4; ++seat) {
        hand_counts[seat] = static_cast<std::uint32_t>(deal.hands[seat].size());
    }
    const std::vector<std::uint64_t> hand_ids = flatten(deal);

    for (const std::int32_t target : {TOUNIU_PARTNERSHIP_NORTH_SOUTH,
                                      TOUNIU_PARTNERSHIP_EAST_WEST}) {
        TouniuAnalyzeRequest request = make_request(deal, hand_ids, 4U, target);
        request.hand_counts = hand_counts.data();
        TouniuBestMoveOutput best{};
        ASSERT_EQ(touniu_solver_best_move_v4(&request, &best), TOUNIU_SOLVER_OK);
        EXPECT_EQ(best.error, TOUNIU_SOLVER_OK);
        EXPECT_EQ(best.is_pass, 0U);

        // The same position through the per-action entry point must contain the
        // chosen card with the value v4 reported, and no value may beat it.
        request.abi_version = TOUNIU_SOLVER_ABI_VERSION_V2;
        std::vector<TouniuAnalyzeAction> records(16);
        std::uint32_t required = 0;
        ASSERT_EQ(
            touniu_solver_analyze_actions_v2(
                &request, records.data(), static_cast<std::uint32_t>(records.size()), &required),
            TOUNIU_SOLVER_OK);
        ASSERT_GT(required, 0U);
        std::int32_t extreme = target == TOUNIU_PARTNERSHIP_NORTH_SOUTH
            ? -1 : 1000;
        bool found = false;
        for (std::uint32_t index = 0; index < required; ++index) {
            const TouniuAnalyzeAction& record = records[index];
            if (target == TOUNIU_PARTNERSHIP_NORTH_SOUTH) {
                extreme = std::max(extreme, record.final_tricks);
            } else {
                extreme = std::min(extreme, record.final_tricks);
            }
            if (record.card_id == best.card_id && record.mode == best.mode) {
                found = true;
                EXPECT_EQ(record.final_tricks, best.tricks);
            }
        }
        EXPECT_TRUE(found);
        EXPECT_EQ(best.tricks, extreme);
    }
}

TEST(CapiV4Test, RejectsWrongVersionAndNullOutput) {
    const LiveDeal deal = make_deal();
    const std::vector<std::uint64_t> hand_ids = flatten(deal);
    TouniuAnalyzeRequest request = make_request(deal, hand_ids, 2U, 0);
    TouniuBestMoveOutput best{};
    EXPECT_EQ(
        touniu_solver_best_move_v4(&request, &best),
        TOUNIU_SOLVER_UNSUPPORTED_VERSION);
    request.abi_version = 4U;
    EXPECT_EQ(
        touniu_solver_best_move_v4(&request, nullptr),
        TOUNIU_SOLVER_NULL_ARGUMENT);
}

}  // namespace
}  // namespace touniu_solver
