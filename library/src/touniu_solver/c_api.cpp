#include <touniu_solver/c_api.h>

#include <optional>

#include <touniu_solver/api.hpp>

namespace {

using namespace touniu_solver;

auto normal_suit(const std::int32_t value) -> std::optional<Suit>
{
    if (value < TOUNIU_SUIT_SPADES || value > TOUNIU_SUIT_CLUBS) {
        return std::nullopt;
    }
    return static_cast<Suit>(value);
}

auto make_card(const TouniuCardInput& input) -> std::optional<Card>
{
    const CardId id{input.id};
    const auto suit = normal_suit(input.suit);
    const auto rank = Rank::from_millionths(input.rank_millionths);

    switch (input.kind) {
    case TOUNIU_CARD_NORMAL:
        if (!suit.has_value() || !rank.has_value() ||
            input.rank_millionths % 1'000'000 != 0 ||
            input.rank_millionths > 14'000'000) {
            return std::nullopt;
        }
        return Card::normal(id, *suit, *rank);
    case TOUNIU_CARD_FLOATING: {
        const auto floating_rank = Rank::floating(input.rank_millionths);
        if (!suit.has_value() || !floating_rank.has_value()) {
            return std::nullopt;
        }
        return Card::floating(id, *suit, *floating_rank);
    }
    case TOUNIU_CARD_FOUR_SUIT:
        if (!rank.has_value() || input.rank_millionths > 14'999'999) {
            return std::nullopt;
        }
        return Card::four_suit(id, *rank);
    case TOUNIU_CARD_BLOCK:
        return suit.has_value() ? std::optional(Card::block(id, *suit)) : std::nullopt;
    case TOUNIU_CARD_REVERSE:
        return suit.has_value() ? std::optional(Card::reverse(id, *suit)) : std::nullopt;
    case TOUNIU_CARD_PLUS_TWO:
        return suit.has_value() ? std::optional(Card::plus_two(id, *suit)) : std::nullopt;
    case TOUNIU_CARD_NO_TRUMP:
        if (input.suit != TOUNIU_SUIT_NO_TRUMP || !rank.has_value()) {
            return std::nullopt;
        }
        return Card::no_trump(id, *rank);
    case TOUNIU_CARD_BIG_JOKER:
        return input.suit == TOUNIU_SUIT_NONE
            ? std::optional(Card::big_joker(id)) : std::nullopt;
    case TOUNIU_CARD_SMALL_JOKER:
        return input.suit == TOUNIU_SUIT_NONE
            ? std::optional(Card::small_joker(id)) : std::nullopt;
    case TOUNIU_CARD_CHERRY_BOMB:
        return suit.has_value()
            ? std::optional(Card::cherry_bomb(id, *suit)) : std::nullopt;
    case TOUNIU_CARD_THREE_SUIT_CAT:
        return input.suit == TOUNIU_SUIT_NONE
            ? std::optional(Card::three_suit_cat(id)) : std::nullopt;
    default:
        return std::nullopt;
    }
}

auto fail(TouniuSolveOutput& output, const TouniuSolverError error) -> std::int32_t
{
    output.error = error;
    return error;
}

}  // namespace

extern "C" auto touniu_solver_solve(
    const TouniuSolveRequest* request,
    TouniuSolveOutput* output) -> std::int32_t
{
    if (output == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    *output = {};
    output->mode = TOUNIU_PLAY_NONE;
    output->effective_suit = TOUNIU_SUIT_NONE;
    if (request == nullptr) {
        return fail(*output, TOUNIU_SOLVER_NULL_ARGUMENT);
    }
    if (request->abi_version != TOUNIU_SOLVER_ABI_VERSION) {
        return fail(*output, TOUNIU_SOLVER_UNSUPPORTED_VERSION);
    }
    if ((request->card_count != 0U && request->cards == nullptr) ||
        request->next_player < TOUNIU_SEAT_WEST ||
        request->next_player > TOUNIU_SEAT_SOUTH ||
        request->direction < TOUNIU_DIRECTION_FORWARD ||
        request->direction > TOUNIU_DIRECTION_REVERSE ||
        request->contract < TOUNIU_CONTRACT_SPADES ||
        request->contract > TOUNIU_CONTRACT_NO_TRUMP ||
        request->target_partnership < TOUNIU_PARTNERSHIP_NORTH_SOUTH ||
        request->target_partnership > TOUNIU_PARTNERSHIP_EAST_WEST) {
        return fail(*output, TOUNIU_SOLVER_INVALID_REQUEST);
    }

    SolveRequest typed;
    typed.scenario.next_player = static_cast<Seat>(request->next_player);
    typed.scenario.direction = static_cast<Direction>(request->direction);
    typed.scenario.contract = static_cast<Contract>(request->contract);
    typed.scenario.tricks_remaining = request->tricks_remaining;
    if (request->has_seed != 0U) {
        typed.scenario.seed = request->seed;
    }
    typed.target = static_cast<Partnership>(request->target_partnership);
    typed.use_alpha_beta = request->use_alpha_beta != 0U;

    for (std::uint32_t index = 0; index < request->card_count; ++index) {
        const TouniuCardInput& input = request->cards[index];
        if (input.seat < TOUNIU_SEAT_WEST || input.seat > TOUNIU_SEAT_SOUTH) {
            return fail(*output, TOUNIU_SOLVER_INVALID_CARD);
        }
        auto card = make_card(input);
        if (!card.has_value()) {
            return fail(*output, TOUNIU_SOLVER_INVALID_CARD);
        }
        typed.scenario.hands[static_cast<std::size_t>(input.seat)].push_back(*card);
    }

    const SolveResponse response = solve_scenario(typed);
    if (response.error != SolveError::None || !response.result.has_value()) {
        return fail(*output, TOUNIU_SOLVER_INVALID_REQUEST);
    }

    output->error = TOUNIU_SOLVER_OK;
    output->tricks = response.result->tricks;
    if (response.result->best_action.has_value()) {
        const Action& action = *response.result->best_action;
        output->has_action = 1U;
        output->is_pass = action.is_pass() ? 1U : 0U;
        if (!action.is_pass()) {
            output->card_id = action.card_id()->value;
            output->mode = static_cast<std::int32_t>(*action.mode());
            output->effective_suit =
                static_cast<std::int32_t>(*action.effective_suit());
        }
    }
    return TOUNIU_SOLVER_OK;
}
