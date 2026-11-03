import fs from 'node:fs';
import { createRequire } from 'node:module';
import { parentPort } from 'node:worker_threads';

const ABI_VERSION_V2 = 2;
const OK = 0;
const CARD_SIZE = 24;
const ACTION_SIZE = 32;
const REQUEST_SIZE = 104;
const CARD_NORMAL = 0;

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

const DECLARERS = [
  { nextPlayer: 2, target: 0 },
  { nextPlayer: 0, target: 0 },
  { nextPlayer: 3, target: 1 },
  { nextPlayer: 1, target: 1 },
];
const STRAIN_CODES = [4, 0, 1, 2, 3];

let runtime = null;

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

function createRuntime(jsPath, wasmPath) {
  const require = createRequire(import.meta.url);
  return Promise.resolve(require(jsPath)({
    wasmBinary: fs.readFileSync(wasmPath),
  })).then((module) => {
    const set = module.setValue;
    const get = module.getValue;
    const u32 = (ptr, value) => set(ptr, value, 'i32');
    const u8 = (ptr, value) => set(ptr, value, 'i8');
    const u64 = (ptr, value) => {
      const big = BigInt(value);
      set(ptr, Number(big & 0xffffffffn), 'i32');
      set(ptr + 4, Number((big >> 32n) & 0xffffffffn), 'i32');
    };

    const maxCards = 52;
    const cardsPtr = module._malloc(maxCards * CARD_SIZE);
    const handsPtr = module._malloc(maxCards * 8);
    const countsPtr = module._malloc(16);
    const wonPtr = module._malloc(16);
    const outputPtr = module._malloc(4 * 4);
    const requiredPtr = module._malloc(4);
    const requestPtr = module._malloc(4 * REQUEST_SIZE);
    for (let index = 0; index < 4; index += 1) {
      u32(wonPtr + index * 4, 0);
    }

    const runStrain = (cards, hands, seed, strain) => {
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

      for (let declarer = 0; declarer < 4; declarer += 1) {
        const ptr = requestPtr + declarer * REQUEST_SIZE;
        u32(ptr + REQ.abiVersion, ABI_VERSION_V2);
        u32(ptr + REQ.cards, cardsPtr);
        u32(ptr + REQ.cardCount, cardIndex);
        u32(ptr + REQ.handCardIds, handsPtr);
        u32(ptr + REQ.handCounts, countsPtr);
        u32(ptr + REQ.currentTrick, 0);
        u32(ptr + REQ.currentTrickCount, 0);
        u32(ptr + REQ.nextPlayer, DECLARERS[declarer].nextPlayer);
        u32(ptr + REQ.direction, 0);
        u32(ptr + REQ.contract, STRAIN_CODES[strain]);
        u32(ptr + REQ.targetPartnership, DECLARERS[declarer].target);
        u32(ptr + REQ.tricksRemaining, cards);
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
      }

      const status = module._touniu_solver_analyze_contract_batch_v3(
        requestPtr,
        4,
        outputPtr,
        4,
        requiredPtr,
      );
      if (status !== OK) throw new Error(`v3 status ${status}`);
      const values = [];
      for (let declarer = 0; declarer < 4; declarer += 1) {
        values.push(get(outputPtr + declarer * 4, 'i32'));
      }
      return values;
    };

    return { runStrain };
  });
}

parentPort.on('message', async (message) => {
  if (message.type === 'init') {
    runtime = await createRuntime(message.jsPath, message.wasmPath);
    parentPort.postMessage({ type: 'ready' });
    return;
  }
  if (message.type === 'task') {
    const started = process.hrtime.bigint();
    const hands = buildDeal(message.cards, message.seed);
    const values = runtime.runStrain(message.cards, hands, message.seed, message.strain);
    const elapsed = Number(process.hrtime.bigint() - started) / 1e6;
    parentPort.postMessage({
      type: 'result',
      id: message.id,
      strain: message.strain,
      values,
      ms: elapsed,
    });
  }
});
