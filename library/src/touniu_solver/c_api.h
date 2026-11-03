#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOUNIU_SOLVER_ABI_VERSION 1U

enum TouniuSolverError {
    TOUNIU_SOLVER_OK = 0,
    TOUNIU_SOLVER_NULL_ARGUMENT = 1,
    TOUNIU_SOLVER_UNSUPPORTED_VERSION = 2,
    TOUNIU_SOLVER_INVALID_REQUEST = 3,
    TOUNIU_SOLVER_INVALID_CARD = 4,
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

#ifdef __cplusplus
}
#endif
