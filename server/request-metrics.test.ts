import { it, expect } from "vitest";
import { createRequestMetrics } from "./request-metrics.mjs";
it("isolates school request counts, expires old buckets and bounds retained schools", () => {
  let time = 1_000_000;
  const metrics = createRequestMetrics({ now: () => time, maxSchools: 2 });
  metrics.record("one", 200, 10);
  metrics.record("one", 503, 30);
  metrics.record("two", 403, 99);
  expect(metrics.snapshot("one")).toEqual({
    requests: 2,
    serverErrors: 1,
    averageMs: 20,
    maxMs: 30,
  });
  expect(metrics.snapshot("two").serverErrors).toBe(0);
  metrics.record("three", 200, 1);
  expect(metrics.snapshot("one").requests).toBe(0);
  time += 16 * 60000;
  expect(metrics.snapshot("two").requests).toBe(0);
});
