import { expect, it } from "vitest";
import { DatabaseSync } from "node:sqlite";
import { cacheStatements } from "./statement-cache.mjs";
it("reuses compilation with bounded capacity, fresh data and fresh tenant bindings", () => {
  const db = new DatabaseSync(":memory:");
  try {
    db.exec(
      "CREATE TABLE t(school TEXT,value TEXT);INSERT INTO t VALUES('a','one'),('b','two')",
    );
    const cache = cacheStatements(db, 2);
    const query = "SELECT value FROM t WHERE school=?";
    const first = db.prepare(query);
    expect(first.get("a")!.value).toBe("one");
    expect(db.prepare(query)).toBe(first);
    expect(db.prepare(query).get("b")!.value).toBe("two");
    db.exec("UPDATE t SET value='updated' WHERE school='a'");
    expect(db.prepare(query).get("a")!.value).toBe("updated");
    db.prepare("SELECT 1").get();
    db.prepare("SELECT 2").get();
    expect(cache.snapshot().size).toBe(2);
    expect(db.prepare(query)).not.toBe(first);
  } finally {
    db.close();
  }
});
