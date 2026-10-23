// Run against a local disposable fixture, never the school's production data.
import { performance } from "node:perf_hooks";
const base = new URL(process.env.LOAD_BASE || "http://127.0.0.1:18877");
if (!["127.0.0.1", "localhost", "[::1]"].includes(base.hostname))
  throw new Error("仅允许本地隔离压测");
const cookie = process.env.LOAD_COOKIE;
if (!cookie)
  throw new Error(
    "请通过 LOAD_COOKIE 环境变量提供隔离测试账户会话（不要放入命令参数或提交到仓库）",
  );
const concurrency = Number(process.env.LOAD_CONCURRENCY || 20);
const requests = Number(process.env.LOAD_REQUESTS || 200);
if (
  ![concurrency, requests].every(Number.isSafeInteger) ||
  concurrency < 1 ||
  concurrency > 100 ||
  requests < 1 ||
  requests > 10000
)
  throw new Error("并发须为 1–100，请求数须为 1–10000");
const durations = [];
let next = 0,
  failures = 0;
const started = performance.now();
await Promise.all(
  Array.from({ length: concurrency }, async () => {
    while (next < requests) {
      const index = next++,
        begin = performance.now();
      try {
        const route = ["/api/assignments", "/api/submissions"][index % 2];
        const response = await fetch(new URL(route, base), {
          headers: { cookie },
          signal: AbortSignal.timeout(10000),
          redirect: "error",
        });
        const data = await response.json();
        if (!response.ok || !Array.isArray(data)) failures++;
      } catch {
        failures++;
      }
      durations.push(performance.now() - begin);
    }
  }),
);
durations.sort((a, b) => a - b);
const percentile = (p) =>
  Math.round(durations[Math.max(0, Math.ceil(durations.length * p) - 1)]);
console.log(
  JSON.stringify(
    {
      requests,
      concurrency,
      failures,
      durationMs: Math.round(performance.now() - started),
      p50Ms: percentile(0.5),
      p95Ms: percentile(0.95),
      p99Ms: percentile(0.99),
    },
    null,
    2,
  ),
);
if (failures) process.exitCode = 1;
