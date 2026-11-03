#!/usr/bin/env node
/**
 * Touniu solver WASM benchmark for the fixed deal protocol.
 *
 * Usage:
 *   node touniu_solver_wasm_benchmark.mjs EMSCRIPTEN_JS WASM_FILE \
 *       [--cards n] [--samples k] [--seed s]
 *
 * Deals are built with the same deterministic algorithm as the native
 * benchmark: 52 normal cards ordered S,H,D,C over ranks 2..A, an xorshift32
 * Fisher-Yates shuffle seeded with seed + sample_index, and 4n cards dealt
 * W,N,E,S round-robin.
 *
 * Each sample calls ABI v2 `analyze_actions` once to negotiate the record
 * buffer (untimed) and once for the measured run, matching the documented
 * benchmark protocol. A single n=1 deal is solved first as a warm-up.
 */
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

const ABI_VERSION_V2 = 2;
const BUFFER_TOO_SMALL = 5;
const OK = 0;
const CARD_SIZE = 24;
const ACTION_SIZE = 32;
const REQUEST_SIZE = 104;
const CARD_NORMAL = 0;
const SEAT_WEST = 0;
const DIRECTION_FORWARD = 0;
const CONTRACT_NO_TRUMP = 4;
const PARTNERSHIP_NORTH_SOUTH = 0;

const REQ = {
  abiVersion: 0,
  cards: 4,
  cardCount: 8,
  handCardIds: 12,
  handCounts: 16,
  currentTrick: 20,
  currentTrickCount: 24,
  nextPlayer: 28,
  direction: 32,
  contract: 36,
  targetPartnership: 40,
  tricksRemaining: 44,
  tricksWon: 48,
  hasPendingBlock: 52,
  pendingBlockSeat: 56,
  validReverseCount: 60,
  seed: 64,
  trickStartDrawIndex: 72,
  drawIndex: 80,
  generatedCardIds: 88,
  generatedCardIdCount: 92,
  useAlphaBeta: 96,
};

function fail(message) {
  console.error(message);
  process.exit(1);
}

function parseArgs(argv) {
  const args = { cards: 8, samples: 1, seed: 591601503 };
  for (let index = 0; index < argv.length; index += 1) {
    const flag = argv[index];
    const value = Number(argv[index + 1]);
    if (flag === "--cards" && Number.isFinite(value)) {
      args.cards = value;
      index += 1;
    } else if (flag === "--samples" && Number.isFinite(value)) {
      args.samples = value;
      index += 1;
    } else if (flag === "--seed" && Number.isFinite(value)) {
      args.seed = value >>> 0;
      index += 1;
    }
  }
  args.cards = Math.min(Math.max(Math.trunc(args.cards), 1), 13);
  args.samples = Math.max(Math.trunc(args.samples), 1);
  return args;
}

function xorshift32(state) {
  let value = state >>> 0;
  value ^= value << 13;
  value >>>= 0;
  value ^= value >>> 17;
  value >>>= 0;
  value ^= value << 5;
  return value >>> 0;
}

function makeDeck() {
  const suits = [0, 1, 2, 3];
  const deck = [];
  let id = 1n;
  for (const suit of suits) {
    for (let rank = 2; rank <= 14; rank += 1) {
      deck.push({
        id: id++,
        kind: CARD_NORMAL,
        rankMillionths: rank * 1_000_000,
        suitMask: 1 << suit,
      });
    }
  }
  return deck;
}

function buildDeal(cards, seed) {
  const deck = makeDeck();
  let state = seed >>> 0;
  for (let index = 51; index >= 1; index -= 1) {
    state = xorshift32(state);
    const swap = state % (index + 1);
    const hold = deck[index];
    deck[index] = deck[swap];
    deck[swap] = hold;
  }
  const hands = [[], [], [], []];
  for (let index = 0; index < cards * 4; index += 1) {
    hands[index % 4].push(deck[index]);
  }
  return hands;
}

async function main() {
  const [jsPath, wasmPath, ...rest] = process.argv.slice(2);
  if (!jsPath || !wasmPath) {
    fail("usage: node touniu_solver_wasm_benchmark.mjs JS WASM [--cards n] [--samples k]");
  }
  const args = parseArgs(rest);

  const factory = require(path.resolve(jsPath));
  const module = await factory({
    wasmBinary: fs.readFileSync(path.resolve(wasmPath)),
  });

  const u32 = (ptr, value) => module.setValue(ptr, value, "i32");
  const u8 = (ptr, value) => module.setValue(ptr, value, "i8");
  const u64 = (ptr, value) => {
    const big = BigInt(value);
    module.setValue(ptr, Number(big & 0xffffffffn), "i32");
    module.setValue(ptr + 4, Number((big >> 32n) & 0xffffffffn), "i32");
  };

  const maxCards = args.cards * 4;
  const cardsPtr = module._malloc(maxCards * CARD_SIZE);
  const handsPtr = module._malloc(maxCards * 8);
  const countsPtr = module._malloc(4 * 4);
  const wonPtr = module._malloc(4 * 4);
  const requiredPtr = module._malloc(4);
  const requestPtr = module._malloc(REQUEST_SIZE);
  const recordsPtr = module._malloc(64 * ACTION_SIZE);

  for (let index = 0; index < 4; index += 1) {
    u32(wonPtr + index * 4, 0);
  }

  const runSample = (cards, seed, measure) => {
    const hands = buildDeal(cards, seed);
    let cardIndex = 0;
    for (const hand of hands) {
      for (const card of hand) {
        const base = cardsPtr + cardIndex * CARD_SIZE;
        u64(base, card.id);
        u32(base + 8, card.kind);
        u32(base + 12, card.rankMillionths);
        u32(base + 16, card.suitMask);
        u64(handsPtr + cardIndex * 8, card.id);
        cardIndex += 1;
      }
    }
    for (let seat = 0; seat < 4; seat += 1) {
      u32(countsPtr + seat * 4, hands[seat].length);
    }

    u32(requestPtr + REQ.abiVersion, ABI_VERSION_V2);
    u32(requestPtr + REQ.cards, cardsPtr);
    u32(requestPtr + REQ.cardCount, cardIndex);
    u32(requestPtr + REQ.handCardIds, handsPtr);
    u32(requestPtr + REQ.handCounts, countsPtr);
    u32(requestPtr + REQ.currentTrick, 0);
    u32(requestPtr + REQ.currentTrickCount, 0);
    u32(requestPtr + REQ.nextPlayer, SEAT_WEST);
    u32(requestPtr + REQ.direction, DIRECTION_FORWARD);
    u32(requestPtr + REQ.contract, CONTRACT_NO_TRUMP);
    u32(requestPtr + REQ.targetPartnership, PARTNERSHIP_NORTH_SOUTH);
    u32(requestPtr + REQ.tricksRemaining, cards);
    u32(requestPtr + REQ.tricksWon, wonPtr);
    u8(requestPtr + REQ.hasPendingBlock, 0);
    u32(requestPtr + REQ.pendingBlockSeat, -1);
    u32(requestPtr + REQ.validReverseCount, 0);
    u32(requestPtr + REQ.seed, seed);
    u64(requestPtr + REQ.trickStartDrawIndex, 0);
    u64(requestPtr + REQ.drawIndex, 0);
    u32(requestPtr + REQ.generatedCardIds, 0);
    u32(requestPtr + REQ.generatedCardIdCount, 0);
    u8(requestPtr + REQ.useAlphaBeta, 1);

    let status = module._touniu_solver_analyze_actions_v2(
      requestPtr,
      0,
      0,
      requiredPtr,
    );
    if (status !== BUFFER_TOO_SMALL) {
      fail(`buffer negotiation status ${status}`);
    }
    const required = module.getValue(requiredPtr, "i32");
    if (required <= 0 || required > 64) {
      fail(`unexpected action count ${required}`);
    }

    let started = 0n;
    if (measure) {
      started = process.hrtime.bigint();
    }
    status = module._touniu_solver_analyze_actions_v2(
      requestPtr,
      recordsPtr,
      required,
      requiredPtr,
    );
    let elapsedMs = 0;
    if (measure) {
      elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
    }
    if (status !== OK) {
      fail(`analyze status ${status}`);
    }
    return elapsedMs;
  };

  // Warm-up: one n=1 deal, excluded from the report.
  runSample(1, 1, false);

  const times = [];
  for (let sample = 0; sample < args.samples; sample += 1) {
    const seed = (args.seed + sample) >>> 0;
    times.push(runSample(args.cards, seed, true));
  }

  const sorted = [...times].sort((left, right) => left - right);
  const sum = times.reduce((total, value) => total + value, 0);
  const median = sorted.length % 2 === 1
    ? sorted[(sorted.length - 1) / 2]
    : (sorted[sorted.length / 2 - 1] + sorted[sorted.length / 2]) / 2;
  const p95Index = Math.max(0, Math.ceil(0.95 * sorted.length) - 1);

  console.log(JSON.stringify({
    cards: args.cards,
    samples: args.samples,
    seed: args.seed,
    average_ms: sum / times.length,
    min_ms: sorted[0],
    max_ms: sorted[sorted.length - 1],
    median_ms: median,
    p95_ms: sorted[p95Index],
    samples_ms: times,
  }));
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
