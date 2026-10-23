// Bounded, process-local rolling statistics; never record URLs, names, bodies or keys.
export function createRequestMetrics({
  now = Date.now,
  maxSchools = 1000,
} = {}) {
  const schools = new Map();
  function record(schoolId, status, durationMs) {
    if (!schoolId) return;
    const minute = Math.floor(now() / 60000);
    let buckets = schools.get(schoolId) || [];
    buckets = buckets.filter((bucket) => bucket.minute > minute - 15);
    let current = buckets.find((bucket) => bucket.minute === minute);
    if (!current) {
      current = { minute, requests: 0, errors: 0, totalMs: 0, maxMs: 0 };
      buckets.push(current);
    }
    current.requests++;
    if (status >= 500) current.errors++;
    current.totalMs += durationMs;
    current.maxMs = Math.max(current.maxMs, durationMs);
    schools.delete(schoolId);
    schools.set(schoolId, buckets);
    if (schools.size > maxSchools) schools.delete(schools.keys().next().value);
  }
  function snapshot(schoolId) {
    const minute = Math.floor(now() / 60000);
    const total = { requests: 0, serverErrors: 0, averageMs: 0, maxMs: 0 };
    let elapsed = 0;
    for (const bucket of schools.get(schoolId) || []) {
      if (bucket.minute <= minute - 15) continue;
      total.requests += bucket.requests;
      total.serverErrors += bucket.errors;
      elapsed += bucket.totalMs;
      total.maxMs = Math.max(total.maxMs, bucket.maxMs);
    }
    total.averageMs = total.requests ? Math.round(elapsed / total.requests) : 0;
    total.maxMs = Math.round(total.maxMs);
    return total;
  }
  return { record, snapshot };
}
