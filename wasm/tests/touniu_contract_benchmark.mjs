#!/usr/bin/env node
/**
 * Contract-table benchmark: independent ABI v2 solves versus the ABI v3
 * same-strain batch inside one WASM module.
 *
 * Usage:
 *   node touniu_contract_benchmark.mjs EMSCRIPTEN_JS WASM_FILE [--cards n]
 *       [--seed s] [--seeds count] [--repeats k]
 *
 * Each deal builds the 20 Playground contract cells (four declarers times five
 * strains). A warm-up run of both paths precedes the timed repeats.
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
const DIRECTION_FORWARD = 0;
const PARTNERSHIP_NS = 0;
const PARTNERSHIP_EW = 1;
const RECORD_CAPACITY = 64;

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

const ACTION = { finalTricks: 24 };

// Declarer rows and the leader/target each one implies.
const DECLARERS = [
  { nextPlayer: 2, target: PARTNERSHIP_NS }, // North -> East leads
  { nextPlayer: 0, target: PARTNERSHIP_NS }, // South -> West leads
  { nextPlayer: 3, target: PARTNERSHIP_EW }, // East -> South leads
  { nextPlayer: 1, target: PARTNERSHIP_EW }, // West -> North leads
];
// Table strain order: NT, S, H, D, C.
const STRAIN_CODES = [4, 0, 1, 2, 3];

function fail(message) {
  console.error(message);
  process.exit(1);
}

function parseArgs(argv) {
  const args = { cards: 7, seed: 591601503, seeds: 5, repeats: 5 };
  for (let index = 0; index < argv.length; index += 1) {
    const flag = argv[index];
    const value = Number(argv[index + 1]);
    if (flag === "--cards" && Number.isFinite(value)) {
      args.cards = Math.min(Math.max(Math.trunc(value), 1), 13);
      index += 1;
    } else if (flag === "--seed" && Number.isFinite(value)) {
      args.seed = value >>> 0;
      index += 1;
    } else if (flag === "--seeds" && Number.isFinite(value)) {
      args.seeds = Math.max(Math.trunc(value), 1);
      index += 1;
    } else if (flag === "--repeats" && Number.isFinite(value)) {
      args.repeats = Math.max(Math.trunc(value), 1);
      index += 1;
    }
  }
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
  const deck = [];
  let id = 1n;
  for (let suit = 0; suit < 4; suit += 1) {
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
    fail("usage: node touniu_contract_benchmark.mjs JS WASM [--cards n]");
  }
  const args = parseArgs(rest);
  const factory = require(path.resolve(jsPath));
  const module = await factory({ wasmBinary: fs.readFileSync(path.resolve(wasmPath)) });

  const set = module.setValue;
  const get = module.getValue;
  const u32 = (ptr, value) => set(ptr, value, "i32");
  const u8 = (ptr, value) => set(ptr, value, "i8");
  const u64 = (ptr, value) => {
    const big = BigInt(value);
    set(ptr, Number(big & 0xffffffffn), "i32");
    set(ptr + 4, Number((big >> 32n) & 0xffffffffn), "i32");
  };

  const maxCards = args.cards * 4;
  const cardsPtr = module._malloc(maxCards * CARD_SIZE);
  const handsPtr = module._malloc(maxCards * 8);
  const countsPtr = module._malloc(16);
  const wonPtr = module._malloc(16);
  const recordsPtr = module._malloc(RECORD_CAPACITY * ACTION_SIZE);
  const outputPtr = module._malloc(4 * 4);
  const requiredPtr = module._malloc(4);
  const requestPtr = module._malloc(4 * REQUEST_SIZE);

  for (let index = 0; index < 4; index += 1) {
    u32(wonPtr + index * 4, 0);
  }

  const writeDeal = (cards, seed) => {
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
    return cardIndex;
  };

  const writeRequest = (ptr, cardCount, declarer, contract, seed) => {
    u32(ptr + REQ.abiVersion, ABI_VERSION_V2);
    u32(ptr + REQ.cards, cardsPtr);
    u32(ptr + REQ.cardCount, cardCount);
    u32(ptr + REQ.handCardIds, handsPtr);
    u32(ptr + REQ.handCounts, countsPtr);
    u32(ptr + REQ.currentTrick, 0);
    u32(ptr + REQ.currentTrickCount, 0);
    u32(ptr + REQ.nextPlayer, DECLARERS[declarer].nextPlayer);
    u32(ptr + REQ.direction, DIRECTION_FORWARD);
    u32(ptr + REQ.contract, contract);
    u32(ptr + REQ.targetPartnership, DECLARERS[declarer].target);
    u32(ptr + REQ.tricksRemaining, args.cards);
    u32(ptr + REQ.tricksWon, wonPtr);
    u8(ptr + REQ.hasPendingBlock, 0);
    u32(ptr + REQ.pendingBlockSeat, -1);
    u32(ptr + REQ.validReverseCount, 0);
    u32(ptr + REQ.seed, seed);
    u64(ptr + REQ.trickStartDrawIndex, 0);
    u64(ptr + REQ.drawIndex, 0);
    u32(ptr + REQ.generatedCardIds, 0);
    u32(ptr + REQ.generatedCardIdCount, 0);
    u8(ptr + REQ.useAlphaBeta, 1);
  };

  const runV2 = (cardCount, seed) => {
    const tricks = new Array(20).fill(0);
    for (let declarer = 0; declarer < 4; declarer += 1) {
      for (let strain = 0; strain < 5; strain += 1) {
        writeRequest(requestPtr, cardCount, declarer, STRAIN_CODES[strain], seed);
        const status = module._touniu_solver_analyze_actions_v2(
          requestPtr,
          recordsPtr,
          RECORD_CAPACITY,
          requiredPtr,
        );
        if (status !== OK) fail(`v2 status ${status}`);
        const count = get(requiredPtr, "i32");
        const values = [];
        for (let record = 0; record < count; record += 1) {
          values.push(get(recordsPtr + record * ACTION_SIZE + ACTION.finalTricks, "i32"));
        }
        // Minimax at the root: the leader optimises its own partnership, which
        // is the declarer's opponents for a contract-table query.
        const target = DECLARERS[declarer].target;
        const rootPartnership = DECLARERS[declarer].nextPlayer === 1 ||
          DECLARERS[declarer].nextPlayer === 3 ? 0 : 1;
        tricks[declarer * 5 + strain] = values.length === 0
          ? 0
          : rootPartnership === target
            ? Math.max(...values)
            : Math.min(...values);
      }
    }
    return tricks;
  };

  const runV3 = (cardCount, seed) => {
    if (typeof module._touniu_solver_analyze_contract_batch_v3 !== "function") {
      fail("missing ABI v3 export");
    }
    const tricks = new Array(20).fill(0);
    for (let strain = 0; strain < 5; strain += 1) {
      for (let declarer = 0; declarer < 4; declarer += 1) {
        writeRequest(
          requestPtr + declarer * REQUEST_SIZE,
          cardCount,
          declarer,
          STRAIN_CODES[strain],
          seed,
        );
      }
      const status = module._touniu_solver_analyze_contract_batch_v3(
        requestPtr,
        4,
        outputPtr,
        4,
        requiredPtr,
      );
      if (status !== OK) fail(`v3 status ${status}`);
      for (let declarer = 0; declarer < 4; declarer += 1) {
        tricks[declarer * 5 + strain] = get(outputPtr + declarer * 4, "i32");
      }
    }
    return tricks;
  };

  // Warm up both paths before timing.
  {
    const seed = args.seed >>> 0;
    const cardCount = writeDeal(args.cards, seed);
    runV2(cardCount, seed);
    runV3(cardCount, seed);
  }

  const deals = [];
  for (let sample = 0; sample < args.seeds; sample += 1) {
    const seed = (args.seed + sample) >>> 0;
    const cardCount = writeDeal(args.cards, seed);
    const reference = runV2(cardCount, seed);
    const timesV2 = [];
    const timesV3 = [];
    let mismatches = 0;
    for (let repeat = 0; repeat < args.repeats; repeat += 1) {
      let started = process.hrtime.bigint();
      runV2(cardCount, seed);
      timesV2.push(Number(process.hrtime.bigint() - started) / 1e6);

      started = process.hrtime.bigint();
      const batch = runV3(cardCount, seed);
      timesV3.push(Number(process.hrtime.bigint() - started) / 1e6);
      for (let index = 0; index < 20; index += 1) {
        if (batch[index] !== reference[index]) mismatches += 1;
      }
    }
    const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
    deals.push({
      seed,
      mismatches,
      mean_ms_v2: mean(timesV2),
      mean_ms_batch: mean(timesV3),
      samples_ms_v2: timesV2,
      samples_ms_batch: timesV3,
    });
  }

  console.log(JSON.stringify({ cards: args.cards, deals }));
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
