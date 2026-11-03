#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOUNIU_SOLVER_ABI_VERSION 1U
#define TOUNIU_SOLVER_ABI_VERSION_V2 2U

enum TouniuSolverError {
    TOUNIU_SOLVER_OK = 0,
    TOUNIU_SOLVER_NULL_ARGUMENT = 1,
    TOUNIU_SOLVER_UNSUPPORTED_VERSION = 2,
    TOUNIU_SOLVER_INVALID_REQUEST = 3,
    TOUNIU_SOLVER_INVALID_CARD = 4,
    TOUNIU_SOLVER_BUFFER_TOO_SMALL = 5,
};

enum TouniuSeat {
    TOUNIU_SEAT_WEST = 0,
    TOUNIU_SEAT_NORTH = 1,
    TOUNIU_SEAT_EAST = 2,
    TOUNIU_SEAT_SOUTH = 3,
};

enum TouniuDirection {
    TOUNIU_DIRECTION_FORWARD = 0,
    TOUNIU_DIRECTION_REVERSE = 1,
};

enum TouniuSuit {
    TOUNIU_SUIT_NONE = -1,
    TOUNIU_SUIT_SPADES = 0,
    TOUNIU_SUIT_HEARTS = 1,
    TOUNIU_SUIT_DIAMONDS = 2,
    TOUNIU_SUIT_CLUBS = 3,
    TOUNIU_SUIT_NO_TRUMP = 4,
};

enum TouniuContract {
    TOUNIU_CONTRACT_SPADES = 0,
    TOUNIU_CONTRACT_HEARTS = 1,
    TOUNIU_CONTRACT_DIAMONDS = 2,
    TOUNIU_CONTRACT_CLUBS = 3,
    TOUNIU_CONTRACT_NO_TRUMP = 4,
};

enum TouniuPartnership {
    TOUNIU_PARTNERSHIP_NORTH_SOUTH = 0,
    TOUNIU_PARTNERSHIP_EAST_WEST = 1,
};

enum TouniuCardKind {
    TOUNIU_CARD_NORMAL = 0,
    TOUNIU_CARD_FLOATING = 1,
    TOUNIU_CARD_FOUR_SUIT = 2,
    TOUNIU_CARD_BLOCK = 3,
    TOUNIU_CARD_REVERSE = 4,
    TOUNIU_CARD_PLUS_TWO = 5,
    TOUNIU_CARD_NO_TRUMP = 6,
    TOUNIU_CARD_BIG_JOKER = 7,
    TOUNIU_CARD_SMALL_JOKER = 8,
    TOUNIU_CARD_CHERRY_BOMB = 9,
    TOUNIU_CARD_THREE_SUIT_CAT = 10,
};

enum TouniuPlayMode {
    TOUNIU_PLAY_NONE = -1,
    TOUNIU_PLAY_LEAD = 0,
    TOUNIU_PLAY_FOLLOW = 1,
    TOUNIU_PLAY_DISCARD = 2,
    TOUNIU_PLAY_TRUMP = 3,
};

typedef struct TouniuCardInput {
    uint64_t id;
    int32_t seat;
    int32_t kind;
    int32_t suit;
    int32_t rank_millionths;
} TouniuCardInput;

typedef struct TouniuSolveRequest {
    uint32_t abi_version;
    const TouniuCardInput* cards;
    uint32_t card_count;
    int32_t next_player;
    int32_t direction;
    int32_t contract;
    int32_t target_partnership;
    uint32_t tricks_remaining;
    uint32_t seed;
    uint8_t has_seed;
    uint8_t use_alpha_beta;
} TouniuSolveRequest;

typedef struct TouniuSolveOutput {
    int32_t error;
    int32_t tricks;
    uint8_t has_action;
    uint8_t is_pass;
    uint64_t card_id;
    int32_t mode;
    int32_t effective_suit;
} TouniuSolveOutput;

/* Versioned, allocation-free FFI entry point for TouniuBridge/native/WASM. */
int32_t touniu_solver_solve(
    const TouniuSolveRequest* request,
    TouniuSolveOutput* output
);

/*
 * ABI v2: analyze every legal action of an already-partially-played position.
 * `suit_mask` is a bit set over TouniuSuit (1 << suit) encoding the card's
 * intrinsic suits. Hands are id references into `cards`, flattened in
 * W, N, E, S order with per-seat `hand_counts`.
 */
typedef struct TouniuLiveCard {
    uint64_t id;
    int32_t kind;
    int32_t rank_millionths;
    uint32_t suit_mask;
} TouniuLiveCard;

typedef struct TouniuLiveTrickPlay {
    int32_t seat;
    uint8_t is_pass;
    uint64_t card_id;
    int32_t mode;
    int32_t effective_suit;
} TouniuLiveTrickPlay;

typedef struct TouniuAnalyzeRequest {
    uint32_t abi_version;
    const TouniuLiveCard* cards;
    uint32_t card_count;
    const uint64_t* hand_card_ids;
    const uint32_t* hand_counts;
    const TouniuLiveTrickPlay* current_trick;
    uint32_t current_trick_count;
    int32_t next_player;
    int32_t direction;
    int32_t contract;
    int32_t target_partnership;
    uint32_t tricks_remaining;
    const uint32_t* tricks_won;
    uint8_t has_pending_block;
    int32_t pending_block_seat;
    uint32_t valid_reverse_count;
    uint32_t seed;
    uint64_t trick_start_draw_index;
    uint64_t draw_index;
    const uint64_t* generated_card_ids;
    uint32_t generated_card_id_count;
    uint8_t use_alpha_beta;
} TouniuAnalyzeRequest;

typedef struct TouniuAnalyzeAction {
    uint8_t is_pass;
    uint64_t card_id;
    int32_t mode;
    int32_t effective_suit;
    int32_t final_tricks;
} TouniuAnalyzeAction;

/*
 * Writes one record per legal action into `records` (capacity `record_capacity`).
 * A null or undersized buffer writes nothing, stores the true count in
 * `*required_count`, and returns TOUNIU_SOLVER_BUFFER_TOO_SMALL. On success
 * exactly `*required_count` records are written. `required_count` must not be
 * null. Returns TOUNIU_SOLVER_OK on success.
 */
int32_t touniu_solver_analyze_actions_v2(
    const TouniuAnalyzeRequest* request,
    TouniuAnalyzeAction* records,
    uint32_t record_capacity,
    uint32_t* required_count
);

/*
 * ABI v3: solve 1-4 declarers of one deal and strain with a shared, verified
 * transposition table. Every element must be a valid v2 request for the same
 * cards, hands, seed, draw index, direction, contract, tricks won and tricks
 * remaining, differing only in next player and target partnership. Only empty
 * trick boundaries are accepted, and declarers must follow North, South, East,
 * West order (a subset keeps that order), where the leader is the declarer's
 * left-hand opponent.
 *
 * `final_tricks` receives one target-partnership trick count per request.
 * Capacities and counts are element counts, not bytes. A null or undersized
 * output writes nothing, stores request_count in `*required_count`, and returns
 * TOUNIU_SOLVER_BUFFER_TOO_SMALL. `required_count` must not be null.
 */
int32_t touniu_solver_analyze_contract_batch_v3(
    const TouniuAnalyzeRequest* requests,
    uint32_t request_count,
    int32_t* final_tricks,
    uint32_t output_capacity,
    uint32_t* required_count
);

#ifdef __cplusplus
}
#endif
