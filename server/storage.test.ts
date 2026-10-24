import { it, expect } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { DatabaseSync } from "node:sqlite";
import { createBackup, restoreBackup } from "../tools/ops/storage.mjs";
import { seedTeachingFixture } from "./analysis.fixture.mjs";
it("backs up real SQLite and files, restores relocated paths and refuses corruption or overwrite", async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "mes-backup-"));
  const source = path.join(root, "source"),
    target = path.join(root, "backup"),
    restored = path.join(root, "restored");
  fs.mkdirSync(path.join(source, "uploads"), { recursive: true });
  const db = new DatabaseSync(path.join(source, "smart-campus.sqlite"));
  try {
    db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
    seedTeachingFixture(db);
    fs.writeFileSync(path.join(source, "uploads", "a.png"), "fixture-file");
    db.prepare("UPDATE submissions SET file_path=?").run(
      path.join(source, "uploads", "a.png"),
    );
    const first = db.prepare("SELECT id FROM submissions LIMIT 1").get()!;
    fs.writeFileSync(
      path.join(source, "uploads", "tile.jpg"),
      "synthetic tile",
    );
    db.prepare(
      "INSERT INTO submission_tiles VALUES(?,0,0,0,0,100,100,?,'image/jpeg')",
    ).run(first.id, path.join(source, "uploads", "tile.jpg"));
    db.prepare(
      "INSERT INTO upload_sessions VALUES('incomplete','u','s',NULL,NULL,'open','[]',100,0,1,9999999999999,NULL)",
    ).run();
    db.prepare(
      "INSERT INTO upload_parts(session_id,index_no,status,file_path) VALUES('incomplete',0,'receiving',?)",
    ).run(path.join(source, "uploads", "partial.jpg"));
    await createBackup(source, target);
    // Later writes must not leak into the snapshot.
    db.exec("UPDATE submissions SET student_name='later'");
    restoreBackup(target, restored);
    const recovered = new DatabaseSync(
      path.join(restored, "smart-campus.sqlite"),
    );
    try {
      const row = recovered.prepare("SELECT * FROM submissions LIMIT 1").get()!;
      expect(row.student_name).not.toBe("later");
      expect(row.file_path).toBe(path.join(restored, "uploads", "a.png"));
      expect(fs.readFileSync(String(row.file_path), "utf8")).toBe(
        "fixture-file",
      );
      expect(
        recovered.prepare("SELECT COUNT(*) n FROM sessions").get()!.n,
      ).toBe(0);
      expect(
        recovered.prepare("SELECT file_path FROM submission_tiles").get()!
          .file_path,
      ).toBe(path.join(restored, "uploads", "tile.jpg"));
      expect(
        fs.readFileSync(path.join(restored, "uploads", "tile.jpg"), "utf8"),
      ).toBe("synthetic tile");
      expect(
        recovered
          .prepare("SELECT status FROM upload_sessions WHERE id='incomplete'")
          .get()!.status,
      ).toBe("cancelled");
      expect(
        recovered.prepare("SELECT COUNT(*) n FROM upload_parts").get()!.n,
      ).toBe(0);
      expect(
        recovered
          .prepare(
            "SELECT value FROM system_flags WHERE key='restored_budget_review'",
          )
          .get()!.value,
      ).toBe("1");
    } finally {
      recovered.close();
    }
    expect(() => restoreBackup(target, source)).toThrow(/已存在/);
    fs.appendFileSync(path.join(target, "uploads", "a.png"), "corruption");
    expect(() => restoreBackup(target, path.join(root, "invalid"))).toThrow(
      /校验失败/,
    );
    expect(fs.existsSync(path.join(root, "invalid"))).toBe(false);
  } finally {
    db.close();
    fs.rmSync(root, { recursive: true, force: true });
  }
});
