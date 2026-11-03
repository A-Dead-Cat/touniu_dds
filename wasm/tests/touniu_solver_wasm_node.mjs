#!/usr/bin/env node
/**
 * System test: load the Touniu solver WASM module and exercise ABI v2.
 *
 * Usage: node touniu_solver_wasm_node.mjs EMSCRIPTEN_JS WASM_FILE
 */
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

const ABI_VERSION_V2 = 2;
const BUFFER_TOO_SMALL = 5;
const OK = 0;
const SEAT_WEST = 0;
const DIRECTION_FORWARD = 0;
const CONTRACT_NO_TRUMP = 4;
const PARTNERSHIP_EAST_WEST = 1;
const CARD_NORMAL = 0;
const SUIT_SPADES = 0;
const SUIT_HEARTS = 1;
const PLAY_LEAD = 0;

// wasm32 offsets for the flat structs in c_api.h.
const REQUEST_SIZE = 104;
const CARD_SIZE = 24;
const ACTION_SIZE = 32;

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

const ACTION = {
  isPass: 0,
  cardId: 8,
  mode: 16,
  effectiveSuit: 20,
  finalTricks: 24,
};

function fail(message) {
  console.error(message);
  process.exit(1);
}

async function main() {
  const [jsPath, wasmPath] = process.argv.slice(2);
  if (!jsPath || !wasmPath) {
    fail("usage: node touniu_solver_wasm_node.mjs EMSCRIPTEN_JS WASM_FILE");
  }

  const factory = require(path.resolve(jsPath));
  if (typeof factory !== "function") {
    fail("createTouniuSolver is not a function");
  }

  const module = await factory({ wasmBinary: fs.readFileSync(wasmPath) });

  for (const symbol of [
    "_malloc",
    "_free",
    "_touniu_solver_solve",
    "_touniu_solver_analyze_actions_v2",
  ]) {
    if (typeof module[symbol] !== "function") {
      fail(`missing export ${symbol}`);
    }
  }

  const u32 = (ptr, value) => module.setValue(ptr, value, "i32");
  const u8 = (ptr, value) => module.setValue(ptr, value, "i8");
  const u64 = (ptr, value) => {
    const n = BigInt(value);
    module.setValue(ptr, Number(n & 0xffffffffn), "i32");
    module.setValue(ptr + 4, Number((n >> 32n) & 0xffffffffn), "i32");
  };
  const readU64 = (ptr) => {
    const lo = BigInt(module.getValue(ptr, "i32") >>> 0);
    const hi = BigInt(module.getValue(ptr + 4, "i32") >>> 0);
    return (hi << 32n) | lo;
  };
  const writeCard = (ptr, id, kind, rankMillionths, suitMask) => {
    u64(ptr, id);
    u32(ptr + 8, kind);
    u32(ptr + 12, rankMillionths);
    u32(ptr + 16, suitMask);
  };

  const requestPtr = module._malloc(REQUEST_SIZE);
  const cardsPtr = module._malloc(2 * CARD_SIZE);
  const handsPtr = module._malloc(2 * 8);
  const countsPtr = module._malloc(4 * 4);
  const wonPtr = module._malloc(4 * 4);
  const requiredPtr = module._malloc(4);
  let recordsPtr = 0;

  try {
    // West holds the spade ace, East the heart ace, no trump, one trick left.
    writeCard(cardsPtr, 1, CARD_NORMAL, 14_000_000, 1 << SUIT_SPADES);
    writeCard(cardsPtr + CARD_SIZE, 2, CARD_NORMAL, 14_000_000, 1 << SUIT_HEARTS);
    u64(handsPtr, 1);
    u64(handsPtr + 8, 2);
    u32(countsPtr, 1);
    u32(countsPtr + 4, 0);
    u32(countsPtr + 8, 1);
    u32(countsPtr + 12, 0);
    for (let index = 0; index < 4; index += 1) {
      u32(wonPtr + index * 4, 0);
    }

    const writeRequest = (ptr, nextPlayer, targetPartnership) => {
      u32(ptr + REQ.abiVersion, ABI_VERSION_V2);
      u32(ptr + REQ.cards, cardsPtr);
      u32(ptr + REQ.cardCount, 2);
      u32(ptr + REQ.handCardIds, handsPtr);
      u32(ptr + REQ.handCounts, countsPtr);
      u32(ptr + REQ.currentTrick, 0);
      u32(ptr + REQ.currentTrickCount, 0);
      u32(ptr + REQ.nextPlayer, nextPlayer);
      u32(ptr + REQ.direction, DIRECTION_FORWARD);
      u32(ptr + REQ.contract, CONTRACT_NO_TRUMP);
      u32(ptr + REQ.targetPartnership, targetPartnership);
      u32(ptr + REQ.tricksRemaining, 1);
      u32(ptr + REQ.tricksWon, wonPtr);
      u8(ptr + REQ.hasPendingBlock, 0);
      u32(ptr + REQ.pendingBlockSeat, -1);
      u32(ptr + REQ.validReverseCount, 0);
      u32(ptr + REQ.seed, 0);
      u64(ptr + REQ.trickStartDrawIndex, 0);
      u64(ptr + REQ.drawIndex, 0);
      u32(ptr + REQ.generatedCardIds, 0);
      u32(ptr + REQ.generatedCardIdCount, 0);
      u8(ptr + REQ.useAlphaBeta, 1);
    };

    writeRequest(requestPtr, SEAT_WEST, PARTNERSHIP_EAST_WEST);

    let status = module._touniu_solver_analyze_actions_v2(
      requestPtr,
      0,
      0,
      requiredPtr,
    );
    if (status !== BUFFER_TOO_SMALL) {
      fail(`null buffer status ${status}, expected ${BUFFER_TOO_SMALL}`);
    }
    const required = module.getValue(requiredPtr, "i32");
    if (required !== 1) {
      fail(`required_count ${required}, expected 1`);
    }

    recordsPtr = module._malloc(required * ACTION_SIZE);
    status = module._touniu_solver_analyze_actions_v2(
      requestPtr,
      recordsPtr,
      required,
      requiredPtr,
    );
    if (status !== OK) {
      fail(`analyze status ${status}, expected ${OK}`);
    }
    if (module.getValue(requiredPtr, "i32") !== 1) {
      fail("required_count changed after a successful call");
    }

    const isPass = module.getValue(recordsPtr + ACTION.isPass, "i8");
    const cardId = readU64(recordsPtr + ACTION.cardId);
    const mode = module.getValue(recordsPtr + ACTION.mode, "i32");
    const suit = module.getValue(recordsPtr + ACTION.effectiveSuit, "i32");
    const tricks = module.getValue(recordsPtr + ACTION.finalTricks, "i32");

    if (isPass !== 0) fail("expected a card action");
    if (cardId !== 1n) fail(`card_id ${cardId}, expected 1`);
    if (mode !== PLAY_LEAD) fail(`mode ${mode}, expected lead`);
    if (suit !== SUIT_SPADES) fail(`effective_suit ${suit}, expected spades`);
    if (tricks !== 1) fail(`final_tricks ${tricks}, expected 1`);

    if (typeof module._touniu_solver_analyze_contract_batch_v3 !== "function") {
      fail("missing export _touniu_solver_analyze_contract_batch_v3");
    }
    const batchRequests = module._malloc(2 * REQUEST_SIZE);
    const batchOutput = module._malloc(2 * 4);
    const batchRequired = module._malloc(4);
    try {
      // Canonical declarer order: North (leader East, target NS) then East
      // (leader South, target EW).
      writeRequest(batchRequests, SEAT_EAST, PARTNERSHIP_NORTH_SOUTH);
      writeRequest(batchRequests + REQUEST_SIZE, SEAT_SOUTH, PARTNERSHIP_EAST_WEST);
      let batchStatus = module._touniu_solver_analyze_contract_batch_v3(
        batchRequests, 2, 0, 0, batchRequired,
      );
      if (batchStatus !== BUFFER_TOO_SMALL) {
        fail(`batch negotiation status ${batchStatus}`);
      }
      if (module.getValue(batchRequired, "i32") !== 2) {
        fail("batch required count mismatch");
      }
      batchStatus = module._touniu_solver_analyze_contract_batch_v3(
        batchRequests, 2, batchOutput, 2, batchRequired,
      );
      if (batchStatus !== OK) fail(`batch status ${batchStatus}`);

      const expected = [0, 0];
      for (let index = 0; index < 2; index += 1) {
        const request = module._malloc(REQUEST_SIZE);
        try {
          writeRequest(
            request,
            index === 0 ? SEAT_EAST : SEAT_SOUTH,
            index === 0 ? PARTNERSHIP_NORTH_SOUTH : PARTNERSHIP_EAST_WEST,
          );
          const required = module.getValue(requiredPtr, "i32");
          const reference = module._malloc(Math.max(required, 1) * ACTION_SIZE);
          const status = module._touniu_solver_analyze_actions_v2(
            request, reference, required, requiredPtr,
          );
          if (status !== OK) fail(`reference status ${status}`);
          let best = 0;
          for (let record = 0; record < required; record += 1) {
            best = Math.max(
              best,
              module.getValue(reference + record * ACTION_SIZE + ACTION.finalTricks, "i32"),
            );
          }
          expected[index] = best;
          module._free(reference);
        } finally {
          module._free(request);
        }
      }
      for (let index = 0; index < 2; index += 1) {
        if (module.getValue(batchOutput + index * 4, "i32") !== expected[index]) {
          fail(`batch result ${index} differs from ABI v2`);
        }
      }
    } finally {
      module._free(batchRequired);
      module._free(batchOutput);
      module._free(batchRequests);
    }

    console.log("touniu_solver_wasm_node: OK");
  } finally {
    if (recordsPtr) module._free(recordsPtr);
    module._free(requiredPtr);
    module._free(wonPtr);
    module._free(countsPtr);
    module._free(handsPtr);
    module._free(cardsPtr);
    module._free(requestPtr);
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
