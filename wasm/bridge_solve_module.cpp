// Minimal C surface for the upstream double-dummy solver, used by the
// playground when the deck is an ordinary 52-card one. The Touniu solver keeps
// handling special cards; this entry only answers plain bridge positions.
#include <cstdint>

#include <api/dll.h>

namespace {

constexpr int kHands = 4;   // N, E, S, W
constexpr int kSuits = 4;   // S, H, D, C
constexpr int kSlots = 13;

}  // namespace

// `remain` is 16 masks, hand-major: repeat*4 + suit, with bit (rank) set.
// `result` receives 1 + 4*13 ints: cards, suit[13], rank[13], score[13],
// equals[13]. Returns 1 on success, otherwise the DDS error code.
extern "C" int bridge_solve_board(
    const std::uint32_t* remain,
    int trump,
    int first,
    const int* trick_suit,
    const int* trick_rank,
    int trick_count,
    int target,
    int solutions,
    int mode,
    int* result)
{
    if (remain == nullptr || result == nullptr) return -1;
    Deal deal{};
    deal.trump = trump;
    deal.first = first;
    const int plays = trick_count < 0 ? 0 : (trick_count > 3 ? 3 : trick_count);
    for (int index = 0; index < 3; ++index) {
        deal.currentTrickSuit[index] = trick_suit != nullptr ? trick_suit[index] : 0;
        deal.currentTrickRank[index] = trick_rank != nullptr ? trick_rank[index] : 0;
        if (index >= plays) {
            deal.currentTrickSuit[index] = 0;
            deal.currentTrickRank[index] = 0;
        }
    }
    for (int hand = 0; hand < kHands; ++hand) {
        for (int suit = 0; suit < kSuits; ++suit) {
            deal.remainCards[hand][suit] = remain[hand * kSuits + suit];
        }
    }
    FutureTricks future{};
    const int status = SolveBoard(deal, target, solutions, mode, &future, 0);
    if (status != 1) return status;
    result[0] = future.cards;
    for (int index = 0; index < kSlots; ++index) {
        result[1 + index] = future.suit[index];
        result[1 + kSlots + index] = future.rank[index];
        result[1 + 2 * kSlots + index] = future.score[index];
        result[1 + 3 * kSlots + index] = future.equals[index];
    }
    return 1;
}
