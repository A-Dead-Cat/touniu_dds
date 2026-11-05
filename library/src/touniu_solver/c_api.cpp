#include "library/tools/heuristic_policy.hpp"
#include <touniu_solver/c_api.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <touniu_solver/api.hpp>
#include <touniu_solver/live_position.hpp>

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

auto decode_suit_mask(const std::uint32_t mask) -> std::vector<Suit>
{
    std::vector<Suit> suits;
    for (std::uint32_t suit = 0; suit <= TOUNIU_SUIT_NO_TRUMP; ++suit) {
        if ((mask & (1U << suit)) != 0U) {
            suits.push_back(static_cast<Suit>(suit));
        }
    }
    return suits;
}

auto has_only_normal_suits(const std::vector<Suit>& suits) -> bool
{
    return std::all_of(suits.begin(), suits.end(), [](const Suit suit) {
        return suit != Suit::NoTrump;
    });
}

auto make_live_card(const TouniuLiveCard& input) -> std::optional<Card>
{
    const CardId id{input.id};
    const auto rank = Rank::from_millionths(input.rank_millionths);
    const std::vector<Suit> suits = decode_suit_mask(input.suit_mask);

    switch (input.kind) {
    case TOUNIU_CARD_NORMAL:
        if (suits.size() != 1U || suits.front() == Suit::NoTrump ||
            !rank.has_value() ||
            input.rank_millionths % 1'000'000 != 0 ||
            input.rank_millionths > 14'000'000) {
            return std::nullopt;
        }
        return Card::normal(id, suits.front(), *rank);
    case TOUNIU_CARD_FLOATING: {
        const auto floating_rank = Rank::floating(input.rank_millionths);
        if (suits.size() != 1U || suits.front() == Suit::NoTrump ||
            !floating_rank.has_value()) {
            return std::nullopt;
        }
        return Card::floating(id, suits.front(), *floating_rank);
    }
    case TOUNIU_CARD_FOUR_SUIT:
        if (suits.size() != 4U || !has_only_normal_suits(suits) ||
            !rank.has_value() || input.rank_millionths > 14'999'999) {
            return std::nullopt;
        }
        return Card::four_suit(id, *rank);
    case TOUNIU_CARD_BLOCK:
    case TOUNIU_CARD_REVERSE:
    case TOUNIU_CARD_PLUS_TWO:
    case TOUNIU_CARD_CHERRY_BOMB: {
        // The Playground offers specials with one ordinary suit, all four
        // ordinary suits, or the NoTrump suit. Only mixed masks are invalid.
        const bool single_no_trump =
            suits.size() == 1U && suits.front() == Suit::NoTrump;
        if ((suits.size() != 1U && suits.size() != 4U) ||
            (!has_only_normal_suits(suits) && !single_no_trump)) {
            return std::nullopt;
        }
        return Card::special(id, static_cast<CardKind>(input.kind), suits);
    }
    case TOUNIU_CARD_NO_TRUMP:
        if (suits.size() != 1U || suits.front() != Suit::NoTrump ||
            !rank.has_value()) {
            return std::nullopt;
        }
        return Card::no_trump(id, *rank);
    case TOUNIU_CARD_BIG_JOKER:
        return suits.empty() ? std::optional(Card::big_joker(id)) : std::nullopt;
    case TOUNIU_CARD_SMALL_JOKER:
        return suits.empty() ? std::optional(Card::small_joker(id)) : std::nullopt;
    case TOUNIU_CARD_THREE_SUIT_CAT:
        return suits.empty() ? std::optional(Card::three_suit_cat(id)) : std::nullopt;
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

namespace {

auto analyze_actions_request_is_valid(const TouniuAnalyzeRequest& request) -> bool
{
    if (request.next_player < TOUNIU_SEAT_WEST ||
        request.next_player > TOUNIU_SEAT_SOUTH ||
        request.direction < TOUNIU_DIRECTION_FORWARD ||
        request.direction > TOUNIU_DIRECTION_REVERSE ||
        request.contract < TOUNIU_CONTRACT_SPADES ||
        request.contract > TOUNIU_CONTRACT_NO_TRUMP ||
        request.target_partnership < TOUNIU_PARTNERSHIP_NORTH_SOUTH ||
        request.target_partnership > TOUNIU_PARTNERSHIP_EAST_WEST ||
        request.tricks_won == nullptr ||
        (request.card_count != 0U && request.cards == nullptr) ||
        (request.current_trick_count != 0U && request.current_trick == nullptr) ||
        request.hand_counts == nullptr ||
        (request.generated_card_id_count != 0U &&
         request.generated_card_ids == nullptr)) {
        return false;
    }
    if (request.has_pending_block != 0U &&
        (request.pending_block_seat < TOUNIU_SEAT_WEST ||
         request.pending_block_seat > TOUNIU_SEAT_SOUTH)) {
        return false;
    }
    std::uint32_t hand_cards = 0U;
    for (std::uint32_t seat = 0; seat < 4U; ++seat) {
        hand_cards += request.hand_counts[seat];
    }
    return hand_cards == 0U || request.hand_card_ids != nullptr;
}

auto decode_live_position(const TouniuAnalyzeRequest& request)
    -> std::optional<LivePosition>
{
    LivePosition live;
    live.next_player = static_cast<Seat>(request.next_player);
    live.direction = static_cast<Direction>(request.direction);
    live.contract = static_cast<Contract>(request.contract);
    live.tricks_remaining = request.tricks_remaining;
    live.seed = request.seed;
    live.trick_start_draw_index = request.trick_start_draw_index;
    live.draw_index = request.draw_index;
    live.valid_reverse_count = request.valid_reverse_count;
    if (request.has_pending_block != 0U) {
        live.pending_block = static_cast<Seat>(request.pending_block_seat);
    }
    for (std::uint32_t seat = 0; seat < 4U; ++seat) {
        live.tricks_won[seat] = request.tricks_won[seat];
    }

    for (std::uint32_t index = 0; index < request.card_count; ++index) {
        auto card = make_live_card(request.cards[index]);
        if (!card.has_value()) {
            return std::nullopt;
        }
        live.cards.push_back(*card);
    }

    std::uint32_t hand_index = 0U;
    for (std::uint32_t seat = 0; seat < 4U; ++seat) {
        for (std::uint32_t index = 0; index < request.hand_counts[seat]; ++index) {
            live.hands[seat].push_back(CardId{request.hand_card_ids[hand_index++]});
        }
    }

    for (std::uint32_t index = 0; index < request.current_trick_count; ++index) {
        const TouniuLiveTrickPlay& input = request.current_trick[index];
        LiveTrickPlay play;
        play.seat = static_cast<Seat>(input.seat);
        play.is_pass = input.is_pass != 0U;
        play.card_id = CardId{input.card_id};
        if (input.mode != TOUNIU_PLAY_NONE) {
            play.mode = static_cast<PlayMode>(input.mode);
        }
        if (input.effective_suit != TOUNIU_SUIT_NONE) {
            play.effective_suit = static_cast<Suit>(input.effective_suit);
        }
        live.current_trick.push_back(play);
    }

    for (std::uint32_t index = 0; index < request.generated_card_id_count; ++index) {
        live.current_trick_generated_card_ids.push_back(
            CardId{request.generated_card_ids[index]});
    }
    return live;
}

}  // namespace

extern "C" auto touniu_solver_best_move_v4(
    const TouniuAnalyzeRequest* request,
    TouniuBestMoveOutput* output) -> std::int32_t
{
    if (output == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    *output = {};
    if (request == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    if (request->abi_version != TOUNIU_SOLVER_ABI_VERSION_V4) {
        return TOUNIU_SOLVER_UNSUPPORTED_VERSION;
    }
    if (!analyze_actions_request_is_valid(*request)) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    const auto live = decode_live_position(*request);
    if (!live.has_value()) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }
    auto position = Position::create(*live);
    if (!position.has_value()) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    const SearchResult result = solve(
        *position,
        static_cast<Partnership>(request->target_partnership),
        request->use_alpha_beta != 0U);

    output->error = TOUNIU_SOLVER_OK;
    output->tricks = result.tricks;
    const std::optional<Action>& action = result.best_action;
    if (!action.has_value()) {
        output->is_pass = 1U;
        output->card_id = 0U;
        output->mode = TOUNIU_PLAY_NONE;
        output->effective_suit = TOUNIU_SUIT_NONE;
        return TOUNIU_SOLVER_OK;
    }
    output->is_pass = action->is_pass() ? 1U : 0U;
    if (action->is_pass()) {
        output->card_id = 0U;
        output->mode = TOUNIU_PLAY_NONE;
        output->effective_suit = TOUNIU_SUIT_NONE;
    } else {
        output->card_id = action->card_id()->value;
        output->mode = static_cast<std::int32_t>(*action->mode());
        output->effective_suit = static_cast<std::int32_t>(*action->effective_suit());
    }
    return TOUNIU_SOLVER_OK;
}

extern "C" auto touniu_solver_heuristic_move_v5(
    const TouniuAnalyzeRequest* request,
    std::int32_t policy,
    TouniuBestMoveOutput* output) -> std::int32_t
{
    if (output == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    *output = {};
    if (request == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    if (request->abi_version < TOUNIU_SOLVER_ABI_VERSION_V2) {
        return TOUNIU_SOLVER_UNSUPPORTED_VERSION;
    }
    if (!analyze_actions_request_is_valid(*request)) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    const auto live = decode_live_position(*request);
    if (!live.has_value()) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }
    auto position = Position::create(*live);
    if (!position.has_value()) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    const Action action = heuristic_choose(*position, policy);
    output->error = TOUNIU_SOLVER_OK;
    output->tricks = 0;
    output->is_pass = action.is_pass() ? 1U : 0U;
    output->card_id = action.card_id().has_value() ? action.card_id()->value : 0U;
    output->mode = action.mode().has_value() ? static_cast<std::int32_t>(*action.mode()) : 0;
    output->effective_suit = action.effective_suit().has_value()
        ? static_cast<std::int32_t>(*action.effective_suit()) : 0;
    return TOUNIU_SOLVER_OK;
}

extern "C" auto touniu_solver_analyze_actions_v2(
    const TouniuAnalyzeRequest* request,
    TouniuAnalyzeAction* records,
    const std::uint32_t record_capacity,
    std::uint32_t* required_count) -> std::int32_t
{
    if (required_count == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    *required_count = 0U;
    if (request == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    if (request->abi_version != TOUNIU_SOLVER_ABI_VERSION_V2) {
        return TOUNIU_SOLVER_UNSUPPORTED_VERSION;
    }
    if (!analyze_actions_request_is_valid(*request)) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    const auto live = decode_live_position(*request);
    if (!live.has_value()) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    auto position = Position::create(*live);
    if (!position.has_value()) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    const std::vector<ActionAnalysis> analyses = analyze_actions(
        *position,
        static_cast<Partnership>(request->target_partnership),
        request->use_alpha_beta != 0U);

    const std::uint32_t count = static_cast<std::uint32_t>(analyses.size());
    *required_count = count;
    if (records == nullptr || record_capacity < count) {
        return TOUNIU_SOLVER_BUFFER_TOO_SMALL;
    }

    for (std::uint32_t index = 0; index < count; ++index) {
        const Action& action = analyses[index].action;
        TouniuAnalyzeAction& output = records[index];
        output = {};
        output.is_pass = action.is_pass() ? 1U : 0U;
        if (action.is_pass()) {
            output.card_id = 0U;
            output.mode = TOUNIU_PLAY_NONE;
            output.effective_suit = TOUNIU_SUIT_NONE;
        } else {
            output.card_id = action.card_id()->value;
            output.mode = static_cast<std::int32_t>(*action.mode());
            output.effective_suit = static_cast<std::int32_t>(*action.effective_suit());
        }
        output.final_tricks = analyses[index].final_tricks;
    }
    return TOUNIU_SOLVER_OK;
}

namespace {

using namespace touniu_solver;

// Per-instance contract-table tier; one WASM Worker owns one instance.
std::int32_t contract_table_tier = TOUNIU_CONTRACT_TABLE_STANDARD;

// Canonical declarer order used by the batch ABI: North, South, East, West.
// -1 means the leader/target combination is not a legal contract query.
auto declarer_order(const TouniuAnalyzeRequest& request) -> int
{
    if (request.target_partnership == TOUNIU_PARTNERSHIP_NORTH_SOUTH) {
        if (request.next_player == TOUNIU_SEAT_EAST) {
            return 0;
        }
        if (request.next_player == TOUNIU_SEAT_WEST) {
            return 1;
        }
        return -1;
    }
    if (request.target_partnership == TOUNIU_PARTNERSHIP_EAST_WEST) {
        if (request.next_player == TOUNIU_SEAT_SOUTH) {
            return 2;
        }
        if (request.next_player == TOUNIU_SEAT_NORTH) {
            return 3;
        }
    }
    return -1;
}

auto same_live_card(const Card& left, const Card& right) -> bool
{
    return left.id() == right.id() && left.kind() == right.kind() &&
           left.rank() == right.rank() &&
           left.intrinsic_suits() == right.intrinsic_suits();
}

auto batch_compatible(const LivePosition& left, const LivePosition& right) -> bool
{
    if (left.direction != right.direction ||
        left.contract != right.contract ||
        left.tricks_remaining != right.tricks_remaining ||
        left.tricks_won != right.tricks_won ||
        left.seed != right.seed ||
        left.trick_start_draw_index != right.trick_start_draw_index ||
        left.draw_index != right.draw_index ||
        left.valid_reverse_count != right.valid_reverse_count ||
        left.pending_block != right.pending_block ||
        left.hands != right.hands ||
        left.current_trick_generated_card_ids != right.current_trick_generated_card_ids ||
        left.cards.size() != right.cards.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.cards.size(); ++index) {
        if (!same_live_card(left.cards[index], right.cards[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace

extern "C" auto touniu_solver_analyze_contract_batch_v3(
    const TouniuAnalyzeRequest* requests,
    const std::uint32_t request_count,
    std::int32_t* final_tricks,
    const std::uint32_t output_capacity,
    std::uint32_t* required_count) -> std::int32_t
{
    if (required_count == nullptr) {
        return TOUNIU_SOLVER_NULL_ARGUMENT;
    }
    *required_count = 0U;
    if (requests == nullptr || request_count == 0U || request_count > 4U) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }

    std::vector<LivePosition> lives;
    std::vector<Partnership> targets;
    lives.reserve(request_count);
    targets.reserve(request_count);
    int previous_declarer = -1;
    for (std::uint32_t index = 0; index < request_count; ++index) {
        const TouniuAnalyzeRequest& request = requests[index];
        if (request.abi_version != TOUNIU_SOLVER_ABI_VERSION_V2) {
            return TOUNIU_SOLVER_UNSUPPORTED_VERSION;
        }
        if (!analyze_actions_request_is_valid(request)) {
            return TOUNIU_SOLVER_INVALID_REQUEST;
        }
        if (request.current_trick_count != 0U ||
            request.has_pending_block != 0U ||
            request.valid_reverse_count != 0U) {
            return TOUNIU_SOLVER_INVALID_REQUEST;
        }
        const int declarer = declarer_order(request);
        if (declarer < 0 || declarer <= previous_declarer) {
            return TOUNIU_SOLVER_INVALID_REQUEST;
        }
        previous_declarer = declarer;

        auto live = decode_live_position(request);
        if (!live.has_value()) {
            return TOUNIU_SOLVER_INVALID_REQUEST;
        }
        lives.push_back(std::move(*live));
        targets.push_back(static_cast<Partnership>(
            request.target_partnership));
    }

    for (std::size_t index = 1; index < lives.size(); ++index) {
        if (!batch_compatible(lives[0], lives[index])) {
            return TOUNIU_SOLVER_INVALID_REQUEST;
        }
    }

    *required_count = request_count;
    if (final_tricks == nullptr || output_capacity < request_count) {
        return TOUNIU_SOLVER_BUFFER_TOO_SMALL;
    }

    std::vector<ContractQuery> queries;
    queries.reserve(request_count);
    for (std::uint32_t index = 0; index < request_count; ++index) {
        auto position = Position::create(lives[index]);
        if (!position.has_value()) {
            return TOUNIU_SOLVER_INVALID_REQUEST;
        }
        queries.push_back(
            {std::move(*position), targets[index]});
    }

    const ContractTableCapacity capacity =
        contract_table_tier == TOUNIU_CONTRACT_TABLE_LOW_MEMORY
        ? ContractTableCapacity::LowMemory : ContractTableCapacity::Standard;
    const std::vector<int> values = solve_contract_batch(queries, SearchOptions{}, capacity);
    for (std::uint32_t index = 0; index < request_count; ++index) {
        final_tricks[index] = values[index];
    }
    return TOUNIU_SOLVER_OK;
}

extern "C" auto touniu_solver_set_contract_table_tier_v1(const std::int32_t tier)
    -> std::int32_t
{
    if (tier != TOUNIU_CONTRACT_TABLE_STANDARD &&
        tier != TOUNIU_CONTRACT_TABLE_LOW_MEMORY) {
        return TOUNIU_SOLVER_INVALID_REQUEST;
    }
    contract_table_tier = tier;
    return TOUNIU_SOLVER_OK;
}
