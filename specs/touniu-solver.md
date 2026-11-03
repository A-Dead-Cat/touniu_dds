---
capability: touniu-solver
owners: [touniu_solver]
last-updated: 2026-11-03
---

# Touniu Solver

> **Specs vs. doxygen.** This document fixes the game-wide semantics and state
> invariants for the Touniu solver. Concrete C++ types, function signatures and
> error codes belong in the public-header doxygen once the API exists.

## Purpose

`touniu_solver` determines the optimal remaining trick result for a Touniu
Bridge position. It is a new solver beside, rather than a modification of, the
standard DDS bridge solver: Touniu has dynamic card identities, special effects,
five effective suits, optional play modes, unequal hand sizes and a
scenario-defined trick limit. The capability is intended for the game server and
WASM client; it does not change the meaning or ABI of any existing DDS API.

## Canonical model

- Seats are `W`, `N`, `E`, `S`; `NS` and `EW` remain the two partnerships. The
  seat cycle is `[W, N, E, S]`; direction is either forward or reverse around
  that cycle.
- A rank is an exact integer in millionths, never a binary floating-point
  number. Thus `A.000001` is `14,000,001`, `A+1` is `15,000,000`, and `A+2` is
  `16,000,000`. Rendering `11` through `14` as `J`, `Q`, `K`, `A` is a client
  concern and does not alter comparison.
- Every held card has an immutable, unique entity ID. A card's kind, rank and
  intrinsic suits are immutable; an optional play mode is an event property,
  not a mutation of the card. PASS has no card entity and can never occur in a
  hand.
- A normal suit is one of the four ordinary suits. `NT` is a fifth intrinsic
  suit. Under an NT contract it is trump; under a suit contract it is an
  independent ordinary suit. Big and small jokers are always trump: they belong
  to the selected suit trump under a suit contract, and to the NT trump system
  under an NT contract.
- A scenario supplies all initial hands, the leader, direction, contract,
  number of tricks remaining, and a generation seed. The default seed is
  `591601503`; seed plus draw index uniquely determine every `+2` draw. The
  generated sequence contains only non-NT floating cards and never repeats an
  already-existing card entity.
- A trick always has four turns. A seat that is empty, or is selected by a
  pending valid `🚫`, takes a temporary PASS turn. Reaching the scenario's
  trick limit ends the game immediately; all remaining cards are discarded.

## Legal moves and play modes

The solver generates actions, not merely cards. A non-PASS action contains the
selected card and, where necessary, one of `follow`, `discard`, or `trump`.
The selected mode and its effective suit are recorded permanently in the trick.
PASS is generated only for an empty hand or a pending block; it is never a
choice when a card can legally be played.

- Normal and floating cards follow their intrinsic normal suit.
- A four-suit card has all four normal suits and a rainbow suit mark. It may not
  lead while its holder has any ordinary card. With a real normal lead, it must
  follow when its holder has a real card of that suit; otherwise the holder may
  choose follow, discard, or (only under a suit contract) trump. When a suit
  trump or joker leads, it must follow trump if the holder has a real trump or
  joker; otherwise it may follow trump or discard. Under an NT contract, an NT
  or joker lead permits it only as a discard. A four-suit lead frees every
  later player to play any suit.
- A three-suit cat may not lead while its holder has any ordinary card. Under a
  suit contract it has the three non-trump normal suits and rank `A+1`; it is
  never trump. Against a real non-trump, non-NT lead it follows when its holder
  has a real card of that suit, and otherwise may follow or discard. Against a
  trump or NT lead it cannot follow; it may be discarded only if the holder has
  no real led-suit card. Under an NT contract it is a downgraded four-suit card
  of rank `0.999999`; while a holder owns it, that holder cannot discard on a
  normal suit and must play the cat when void in that suit.
- `🚫`, `🔄` and `+2` are valid only when played as follow or trump, never as a
  discard. NT cards, jokers and cherry bombs otherwise use their defined
  intrinsic suit/rank rules.

## Trick resolution and effects

- Equal highest ranks are won by the earlier play.
- With a normal lead, standard comparison applies: eligible lead suit competes,
  then trump wins when the contract supplies a trump system. PASS cannot win.
  A PASS lead permits any later card; trump has priority when applicable,
  otherwise ranks compare directly. Four PASS plays are won by the first play.
- A four-suit lead is always resolved by rank only: no trump priority applies.
- A three-suit-cat lead permits every later suit. Under a suit contract, trump
  has priority and the remaining cards compare by rank. Under an NT contract,
  the NT/joker trump system still has priority and all non-trumps compare by
  rank.
- A cherry bomb which would otherwise win instead awards the trick to the next
  seat in the current direction.
- A valid `🚫` causes the next scheduled player in the same unfinished trick to
  PASS. A `🚫` played fourth has no target and is ineffective. A discarded
  `🚫` is ineffective. The forced-PASS marker is consumed exactly once.
- A valid `🔄` increments this trick's effective-reverse count. When the trick
  is collected, an odd count reverses direction before the next lead. A
  discarded `🔄` does not count.
- A valid `+2` immediately adds two deterministically generated floating cards
  to the next seat in the current direction, regardless of who later wins the
  trick. In particular, a fourth-hand valid `+2` draws for that trick's first
  seat; the draw occurs before winner determination and before the next trick
  is led. A discarded `+2` has no effect.

## Search and transpositions

- The solver evaluates a zero-sum objective for a requested partnership: its
  value is the number of future tricks that partnership can secure with optimal
  play. It uses alpha-beta pruning over all legal actions; search optimisation
  must never remove a legal play or change tie breaking.
- A transposition key includes each complete hand (entity ID and immutable card
  data), contract, leader/next player, direction, all current-trick action
  records including modes and effective suits, pending block target,
  effective-reverse count, tricks remaining, seed and draw index. Positions
  differing in any of these fields are not interchangeable.
- Move application is reversible: search must restore hands, generated-card
  sequence position, current trick, winner bookkeeping, direction and pending
  effects exactly after exploring each child. No mutable state may leak between
  sibling branches.
- One action-analysis request shares a single transposition table across all
  displayed actions. Entries are keyed by a primary identity and accepted only
  when a second independent verifier matches; a collision is rejected, never
  merged.
- Every displayed action is evaluated from its own full
  `(-infinity, +infinity)` window. Alpha-beta and stored bounds are internal to
  the search; sibling cutoffs never narrow another action's window, and only an
  exact result is reported as that action's value.
- The position identity used by the table is maintained incrementally from
  applied and undone actions and includes each seat's own win count. Positions
  that differ only in which partner won an earlier trick are therefore not
  interchangeable. Tests compare the maintained identity with a full
  recomputation after every apply and undo.
- The standard DDS `Pos`, `Moves`, rank-bitmask holdings and DDS transposition
  table are not inputs to this solver. Their fixed four-suit, 13-rank and
  equal-hand-size invariants make reuse unsound. The new solver may share DDS's
  build, testing, context-lifetime and WASM packaging conventions only.

## Key entry points

- `library/src/touniu_solver/api.hpp` exposes the typed C++ request/response
  API used inside DDS.
- `library/src/touniu_solver/c_api.h` exposes ABI version 1 as a flat,
  allocation-free caller interface suitable for a native module or a future
  WASM wrapper. TouniuBridge supplies exact millionth ranks and immutable card
  IDs and receives the optimal trick count plus the best card, play mode and
  effective suit. Unknown ABI versions and malformed cards fail explicitly.
- The C ABI currently accepts positions at a trick boundary. Importing an
  already-partially-played trick will be added before TouniuBridge uses the
  solver for mid-trick advice.
- Existing `dll.h` and standard DDS `SolveBoard` APIs remain unchanged.

## Known gaps / non-goals

- This capability does not parse PBN, calculate standard DD tables, calculate
  scoring, or replace standard DDS bridge analysis.
- It has no chance nodes: `+2` generation is deterministic from the supplied
  seed and draw index.
- UI animation, card artwork, text layout and a player's voluntary interface
  choices are outside the solver. The solver returns legal actions and optimal
  evaluations, not presentation.
