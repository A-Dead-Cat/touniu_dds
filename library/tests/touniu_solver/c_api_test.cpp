#include <touniu_solver/c_api.h>

#include <gtest/gtest.h>

namespace {

auto valid_request(const TouniuCardInput* cards, const std::uint32_t count)
    -> TouniuSolveRequest
{
    return {
        TOUNIU_SOLVER_ABI_VERSION,
        cards,
        count,
        TOUNIU_SEAT_WEST,
        TOUNIU_DIRECTION_FORWARD,
        TOUNIU_CONTRACT_NO_TRUMP,
        TOUNIU_PARTNERSHIP_EAST_WEST,
        1,
        0,
        0,
        1,
    };
}

TEST(CApiTest, SolvesFlatVersionedRequest)
{
    const TouniuCardInput cards[] = {
        {1, TOUNIU_SEAT_WEST, TOUNIU_CARD_NORMAL,
         TOUNIU_SUIT_SPADES, 14'000'000},
    };
    const TouniuSolveRequest request = valid_request(cards, 1);
    TouniuSolveOutput output{};

    const int error = touniu_solver_solve(&request, &output);

    EXPECT_EQ(error, TOUNIU_SOLVER_OK);
    EXPECT_EQ(output.error, TOUNIU_SOLVER_OK);
    EXPECT_EQ(output.tricks, 1);
    EXPECT_EQ(output.has_action, 1);
    EXPECT_EQ(output.is_pass, 0);
    EXPECT_EQ(output.card_id, 1U);
    EXPECT_EQ(output.mode, TOUNIU_PLAY_LEAD);
    EXPECT_EQ(output.effective_suit, TOUNIU_SUIT_SPADES);
}

TEST(CApiTest, RejectsUnknownAbiVersion)
{
    TouniuSolveRequest request = valid_request(nullptr, 0);
    request.abi_version = TOUNIU_SOLVER_ABI_VERSION + 1;
    TouniuSolveOutput output{};

    const int error = touniu_solver_solve(&request, &output);

    EXPECT_EQ(error, TOUNIU_SOLVER_UNSUPPORTED_VERSION);
    EXPECT_EQ(output.error, TOUNIU_SOLVER_UNSUPPORTED_VERSION);
}

TEST(CApiTest, RejectsMalformedFloatingCard)
{
    const TouniuCardInput cards[] = {
        {1, TOUNIU_SEAT_WEST, TOUNIU_CARD_FLOATING,
         TOUNIU_SUIT_SPADES, 2'000'000},
    };
    const TouniuSolveRequest request = valid_request(cards, 1);
    TouniuSolveOutput output{};

    const int error = touniu_solver_solve(&request, &output);

    EXPECT_EQ(error, TOUNIU_SOLVER_INVALID_CARD);
    EXPECT_EQ(output.error, TOUNIU_SOLVER_INVALID_CARD);
}

TEST(CApiTest, RejectsNullPointers)
{
    TouniuSolveOutput output{};
    EXPECT_EQ(touniu_solver_solve(nullptr, &output), TOUNIU_SOLVER_NULL_ARGUMENT);

    const TouniuSolveRequest request = valid_request(nullptr, 0);
    EXPECT_EQ(touniu_solver_solve(&request, nullptr), TOUNIU_SOLVER_NULL_ARGUMENT);
}

}  // namespace
