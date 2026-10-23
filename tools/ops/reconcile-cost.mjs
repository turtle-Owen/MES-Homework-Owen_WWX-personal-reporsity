import { DatabaseSync } from "node:sqlite";
import { randomUUID } from "node:crypto";
import path from "node:path";
import fs from "node:fs";
import { pathToFileURL } from "node:url";
import { price } from "../../server/cost-budget.mjs";
export function reconcileCost(db, id, input, output, note, now = Date.now()) {
  if (
    ![input, output].every(
      (n) => Number.isSafeInteger(n) && n >= 0 && n <= 2_000_000,
    ) ||
    typeof note !== "string" ||
    !note.trim() ||
    note.length > 500
  )
    throw new Error("需要有效的供应商确认用量和对账说明");
  db.exec("BEGIN IMMEDIATE");
  try {
    const row = db.prepare("SELECT * FROM model_costs WHERE id=?").get(id);
    if (
      !row ||
      !["reserved", "dispatched", "unknown"].includes(row.status) ||
      row.updated_at > now - 900000
    )
      throw new Error(
        "仅允许对账超过 15 分钟的未决记录，不能修改已结算或正在调用的记录",
      );
    const charged =
      price(input, row.input_rate) + price(output, row.output_rate);
    db.prepare(
      "INSERT INTO model_cost_reconciliations VALUES(?,?,?,?,?,?)",
    ).run(randomUUID(), id, row.reserved, charged, note.trim(), now);
    db.prepare(
      "INSERT OR REPLACE INTO model_cost_receipts VALUES(?,?,?,?)",
    ).run(id, input, output, now);
    db.prepare(
      "UPDATE model_costs SET status='settled',charged=?,updated_at=? WHERE id=?",
    ).run(charged, now, id);
    db.exec("COMMIT");
    return { chargedMicroCny: charged };
  } catch (e) {
    db.exec("ROLLBACK");
    throw e;
  }
}
export function acknowledgeRestoredBudget(db, note) {
  if (typeof note !== "string" || !note.trim() || note.length > 500)
    throw new Error("必须提供恢复后对账说明");
  db.exec("BEGIN IMMEDIATE");
  try {
    if (
      db
        .prepare(
          "SELECT value FROM system_flags WHERE key='restored_budget_review'",
        )
        .get()?.value !== "1"
    )
      throw new Error("没有待确认的恢复记录");
    db.prepare(
      "INSERT INTO model_cost_reconciliations VALUES(?,?,?,?,?,?)",
    ).run(randomUUID(), "__restore__", 0, 0, note.trim(), Date.now());
    db.prepare(
      "UPDATE system_flags SET value='0' WHERE key='restored_budget_review'",
    ).run();
    db.exec("COMMIT");
  } catch (e) {
    db.exec("ROLLBACK");
    throw e;
  }
}
if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href
) {
  const args = process.argv.slice(2);
  const acknowledgement = args[0] === "--acknowledge-restore";
  const [database, id, input, output, ...note] = acknowledgement
    ? args.slice(1)
    : args;
  let db;
  try {
    if (!database) throw new Error("需要数据库路径");
    if (!fs.existsSync(database)) throw new Error("数据库不存在");
    db = new DatabaseSync(database);
    if (acknowledgement) {
      acknowledgeRestoredBudget(db, args.slice(2).join(" "));
      console.log("已记录恢复后对账确认；现有未决预留仍然占用额度");
    } else {
      if (!id || input === undefined || output === undefined || !note.length)
        throw new Error(
          "用法：node tools/ops/reconcile-cost.mjs <数据库路径> <费用记录ID> <确认输入tokens> <确认输出tokens> <对账说明>",
        );
      console.log(
        JSON.stringify(
          reconcileCost(db, id, Number(input), Number(output), note.join(" ")),
        ),
      );
    }
  } catch (e) {
    console.error(e.message);
    process.exitCode = 1;
  } finally {
    db?.close();
  }
}
