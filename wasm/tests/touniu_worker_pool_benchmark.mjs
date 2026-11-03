#!/usr/bin/env node
/**
 * Worker-pool benchmark: one thread running the five same-strain ABI v3
 * batches sequentially versus the same batches dispatched across a pool of
 * worker threads (the browser scheduling strategy of Stage 6).
 *
 * Usage:
 *   node touniu_worker_pool_benchmark.mjs EMSCRIPTEN_JS WASM_FILE \
 *       [--cards n] [--seed s] [--seeds k] [--repeats k] [--workers k]
 *
 * Threads load the module once (warm-up excluded) and each task computes one
 * strain's four declarers through the shared ABI v3 batch call. The numbers are
 * wall clock on this machine, not browser numbers.
 */
import os from "node:os";
import path from "node:path";
import { Worker } from "node:worker_threads";

const WORKER_URL = new URL("./touniu_pool_worker.mjs", import.meta.url);

function fail(message) {
  console.error(message);
  process.exit(1);
}

function parseArgs(argv) {
  const defaultWorkers = Math.min(4, Math.max(1, os.cpus().length - 1));
  const args = {
    cards: 7,
    seed: 591601503,
    seeds: 5,
    repeats: 5,
    workers: defaultWorkers,
  };
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
    } else if (flag === "--workers" && Number.isFinite(value)) {
      args.workers = Math.min(Math.max(Math.trunc(value), 1), 8);
      index += 1;
    }
  }
  return args;
}

function spawnThread(jsPath, wasmPath) {
  return new Promise((resolve, reject) => {
    const worker = new Worker(WORKER_URL, { type: "module" });
    worker.once("error", reject);
    const ready = message => {
      if (message?.type === "ready") {
        worker.off("message", ready);
        resolve(worker);
      }
    };
    worker.on("message", ready);
    worker.postMessage({
      type: "init",
      jsPath: path.resolve(jsPath),
      wasmPath: path.resolve(wasmPath),
    });
  });
}

let nextTaskId = 1;

function runTask(worker, task) {
  return new Promise((resolve, reject) => {
    const id = nextTaskId++;
    const onMessage = message => {
      if (message?.type !== "result" || message.id !== id) return;
      worker.off("message", onMessage);
      worker.off("error", onError);
      resolve(message);
    };
    const onError = error => {
      worker.off("message", onMessage);
      worker.off("error", onError);
      reject(error);
    };
    worker.on("message", onMessage);
    worker.on("error", onError);
    worker.postMessage({ type: "task", ...task, id });
  });
}

async function runSequential(worker, deal, strains) {
  const started = process.hrtime.bigint();
  const values = [];
  for (const strain of strains) {
    const result = await runTask(worker, {
      cards: deal.cards,
      seed: deal.seed,
      strain,
    });
    values[strain] = result.values;
  }
  return {
    ms: Number(process.hrtime.bigint() - started) / 1e6,
    values,
  };
}

async function runPool(workers, deal, strains) {
  const started = process.hrtime.bigint();
  const values = [];
  const queue = [...strains];
  await Promise.all(workers.map(async (worker) => {
    while (queue.length > 0) {
      const strain = queue.shift();
      const result = await runTask(worker, {
        cards: deal.cards,
        seed: deal.seed,
        strain,
      });
      values[strain] = result.values;
    }
  }));
  return {
    ms: Number(process.hrtime.bigint() - started) / 1e6,
    values,
  };
}

async function main() {
  const [jsPath, wasmPath, ...rest] = process.argv.slice(2);
  if (!jsPath || !wasmPath) {
    fail("usage: node touniu_worker_pool_benchmark.mjs JS WASM [--cards n]");
  }
  const args = parseArgs(rest);
  const strains = [0, 1, 2, 3, 4];
  const workers = await Promise.all(
    Array.from({ length: args.workers }, () => spawnThread(jsPath, wasmPath)),
  );
  const single = workers[0];

  try {
    // Warm both modes once.
    const warm = { cards: args.cards, seed: 1 };
    await runSequential(single, warm, strains);
    await runPool(workers, warm, strains);

    const deals = [];
    for (let sample = 0; sample < args.seeds; sample += 1) {
      const deal = { cards: args.cards, seed: (args.seed + sample) >>> 0 };
      let mismatches = 0;
      const singleTimes = [];
      const poolTimes = [];
      for (let repeat = 0; repeat < args.repeats; repeat += 1) {
        const baseline = await runSequential(single, deal, strains);
        const pooled = await runPool(workers, deal, strains);
        singleTimes.push(baseline.ms);
        poolTimes.push(pooled.ms);
        for (const strain of strains) {
          for (let declarer = 0; declarer < 4; declarer += 1) {
            if (baseline.values[strain][declarer] !== pooled.values[strain][declarer]) {
              mismatches += 1;
            }
          }
        }
      }
      const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
      deals.push({
        seed: deal.seed,
        mismatches,
        mean_ms_single: mean(singleTimes),
        mean_ms_pool: mean(poolTimes),
        samples_ms_single: singleTimes,
        samples_ms_pool: poolTimes,
      });
    }

    console.log(JSON.stringify({
      cards: args.cards,
      workers: args.workers,
      cpu_threads: os.cpus().length,
      deals,
    }));
  } finally {
    await Promise.all(workers.map(worker => worker.terminate()));
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
