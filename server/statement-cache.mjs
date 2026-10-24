// Cache compiled statements only, never rows or authorization results.
export function cacheStatements(db, capacity = 256) {
  const prepare = db.prepare.bind(db),
    cache = new Map();
  let hits = 0,
    misses = 0;
  db.prepare = (sql) => {
    let statement = cache.get(sql);
    if (statement) {
      hits++;
      cache.delete(sql);
    } else {
      misses++;
      statement = prepare(sql);
    }
    cache.set(sql, statement);
    if (cache.size > capacity) cache.delete(cache.keys().next().value);
    return statement;
  };
  return {
    snapshot: () => ({ size: cache.size, capacity, hits, misses }),
    clear: () => cache.clear(),
  };
}
