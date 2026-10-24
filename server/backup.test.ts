import { afterEach, beforeEach, expect, it } from "vitest";
import { DatabaseSync } from "node:sqlite";
import { spawn } from "node:child_process";
import { once } from "node:events";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { randomUUID } from "node:crypto";
import {
  backupSettings,
  backupStatus,
  runBackup,
  retentionPlan,
} from "../tools/ops/backup.mjs";
import {
  createBackup,
  verifyBackup,
  restoreBackup,
} from "../tools/ops/storage.mjs";
import { seedTeachingFixture } from "./analysis.fixture.mjs";
let root: string,
  source: string,
  backups: string,
  db: DatabaseSync,
  config: any;
beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "mes-auto-backup-"));
  source = path.join(root, "data");
  backups = path.join(root, "backups");
  fs.mkdirSync(path.join(source, "uploads"), { recursive: true });
  db = new DatabaseSync(path.join(source, "smart-campus.sqlite"));
  db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
  seedTeachingFixture(db);
  fs.writeFileSync(
    path.join(source, "uploads", "photo.jpg"),
    "synthetic-original",
  );
  db.prepare("UPDATE submissions SET file_path=?").run(
    path.join(source, "uploads", "photo.jpg"),
  );
  config = {
    source,
    root: backups,
    recent: 1,
    days: 1,
    maxTotalBytes: 100 * 1024 ** 2,
    maxSnapshotBytes: 10 * 1024 ** 2,
    minFreeBytes: 1024,
    drillDays: 7,
  };
});
afterEach(() => {
  db.close();
  fs.rmSync(root, { recursive: true, force: true });
});
it("creates a verified snapshot, performs a restore drill and recovers grades and files", async () => {
  const before = db
    .prepare(
      "SELECT id,score,teacher_reviewed,analysis_json FROM submissions ORDER BY id",
    )
    .all();
  const time = Date.now();
  const state = await runBackup(config, { now: () => time });
  expect(state.state).toBe("succeeded");
  expect(state.lastRestoreDrillAt).toBe(new Date(time).toISOString());
  expect(backupStatus(config, { now: () => time + 1000 }).healthy).toBe(true);
  expect(backupStatus(config, { now: () => time + 3 * 3600000 }).healthy).toBe(
    false,
  );
  const snapshot = path.join(backups, state.lastSnapshot);
  expect(Object.keys(verifyBackup(snapshot).manifest.files)).toEqual(
    expect.arrayContaining(["smart-campus.sqlite", "uploads/photo.jpg"]),
  );
  db.exec("UPDATE submissions SET score=0,teacher_reviewed=0");
  restoreBackup(snapshot, path.join(root, "recovered"));
  const recovered = new DatabaseSync(
    path.join(root, "recovered", "smart-campus.sqlite"),
  );
  try {
    expect(
      recovered
        .prepare(
          "SELECT id,score,teacher_reviewed,analysis_json FROM submissions ORDER BY id",
        )
        .all(),
    ).toEqual(before);
    expect(
      fs.readFileSync(
        path.join(root, "recovered", "uploads/photo.jpg"),
        "utf8",
      ),
    ).toBe("synthetic-original");
    expect(recovered.prepare("SELECT COUNT(*) n FROM sessions").get()!.n).toBe(
      0,
    );
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
  expect(
    fs.readdirSync(backups).some((name) => name.startsWith(".restore-")),
  ).toBe(false);
});
it("retains the last good backup on failure and prunes only owned history after success", async () => {
  let time = Date.now();
  const first = await runBackup(config, { now: () => time });
  fs.mkdirSync(path.join(backups, "operator-copy"));
  const foreign = "snapshot-20200101T000000000Z-" + randomUUID();
  fs.mkdirSync(path.join(backups, foreign));
  fs.writeFileSync(
    path.join(backups, foreign, "manifest.json"),
    JSON.stringify({ version: 1, ownerId: "someone-else", files: {} }),
  );
  fs.unlinkSync(path.join(source, "uploads", "photo.jpg"));
  await expect(runBackup(config)).rejects.toThrow();
  expect(backupStatus(config).state).toBe("failed");
  expect(backupStatus(config).lastSnapshot).toBe(first.lastSnapshot);
  expect(fs.existsSync(path.join(backups, first.lastSnapshot))).toBe(true);
  fs.writeFileSync(
    path.join(source, "uploads", "photo.jpg"),
    "synthetic-original",
  );
  time += 2 * 86400000;
  const next = await runBackup(config, { now: () => time });
  expect(next.pruned).toBe(1);
  expect(fs.existsSync(path.join(backups, first.lastSnapshot))).toBe(false);
  expect(fs.existsSync(path.join(backups, next.lastSnapshot))).toBe(true);
  expect(fs.existsSync(path.join(backups, foreign))).toBe(true);
  expect(fs.existsSync(path.join(backups, "operator-copy"))).toBe(true);
});
it("rejects incomplete manifests, low space and oversized snapshots", async () => {
  const state = await runBackup(config);
  const snapshot = path.join(backups, state.lastSnapshot);
  const manifestPath = path.join(snapshot, "manifest.json");
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
  delete manifest.files["uploads/photo.jpg"];
  fs.writeFileSync(manifestPath, JSON.stringify(manifest));
  expect(() => verifyBackup(snapshot)).toThrow(/不完整/);
  await expect(
    createBackup(source, path.join(root, "limited"), { maxBytes: 1 }),
  ).rejects.toThrow(/上限/);
  await expect(
    createBackup(source, path.join(root, "no-space"), {
      minFreeBytes: Number.MAX_SAFE_INTEGER,
    }),
  ).rejects.toThrow(/空间不足/);
  expect(fs.existsSync(path.join(root, "limited"))).toBe(false);
  expect(fs.existsSync(path.join(root, "no-space"))).toBe(false);
});
it("rejects nested/aliased data destinations and foreign sources without altering status", async () => {
  await expect(
    runBackup({ ...config, root: path.join(source, "backups") }),
  ).rejects.toThrow(/之外/);
  const alias = path.join(root, "alias");
  fs.symlinkSync(source, alias, "dir");
  await expect(
    createBackup(source, path.join(alias, "nested")),
  ).rejects.toThrow(/之外/);
  const first = await runBackup(config);
  const other = path.join(root, "other");
  fs.mkdirSync(other);
  await expect(runBackup({ ...config, source: other })).rejects.toThrow(/混用/);
  expect(backupStatus(config).lastSnapshot).toBe(first.lastSnapshot);
  expect(backupStatus(config).state).toBe("succeeded");
});
it("cleans only registered incomplete staging after interruption", async () => {
  config.recent = 2;
  const first = await runBackup(config);
  const identity = JSON.parse(
    fs.readFileSync(path.join(backups, ".backup-root.json"), "utf8"),
  );
  const target = "snapshot-20200101T000000000Z-" + randomUUID();
  const stage = path.join(backups, target + ".partial-" + randomUUID());
  fs.mkdirSync(stage);
  fs.writeFileSync(path.join(stage, "half"), "incomplete");
  const drill = ".restore-" + target;
  fs.mkdirSync(path.join(backups, drill));
  fs.writeFileSync(
    path.join(backups, ".inflight.json"),
    JSON.stringify({ ownerId: identity.ownerId, target, drill }),
  );
  await runBackup(config);
  expect(fs.existsSync(stage)).toBe(false);
  expect(fs.existsSync(path.join(backups, drill))).toBe(false);
  expect(fs.existsSync(path.join(backups, first.lastSnapshot))).toBe(true);
});
it("releases a concurrent process lock after SIGKILL instead of leaving a stale lock", async () => {
  await runBackup(config);
  const helper = path.join(root, "lock.mjs");
  fs.writeFileSync(
    helper,
    `import {DatabaseSync} from 'node:sqlite';const db=new DatabaseSync(${JSON.stringify(path.join(backups, ".backup-lock.sqlite"))});db.exec('BEGIN IMMEDIATE');process.send('locked');setInterval(()=>{},1000);`,
  );
  const child = spawn(process.execPath, [helper], {
    stdio: ["ignore", "ignore", "ignore", "ipc"],
  });
  await once(child, "message");
  try {
    await expect(runBackup(config)).rejects.toThrow(/正在运行/);
  } finally {
    const done = once(child, "exit");
    child.kill("SIGKILL");
    await done;
  }
  expect((await runBackup(config)).state).toBe("succeeded");
});
it("keeps recent/daily representatives within quota and protects the new copy despite a backwards clock", () => {
  const time = Date.parse("2026-10-24T12:00:00Z");
  const rows = [0, 1, 2, 24, 48, 72].map((hours) => ({
    name: String(100 - hours),
    bytes: 10,
    createdAt: new Date(time - hours * 3600000).toISOString(),
  }));
  const policy = { recent: 2, days: 3, maxTotalBytes: 100 };
  expect(retentionPlan(rows, policy, time).map((r) => r.name)).toEqual(["98"]);
  const pruned = retentionPlan(
    rows,
    { ...policy, maxTotalBytes: 10 },
    time,
    "76",
  );
  expect(pruned.some((r) => r.name === "76")).toBe(false);
  expect(rows.length - pruned.length).toBe(1);
});
it("rejects invalid configuration and reports a missing backup as unhealthy", () => {
  expect(() => backupSettings({})).toThrow(/必须配置/);
  expect(() =>
    backupSettings({
      DATA_DIR: source,
      BACKUP_DIR: backups,
      BACKUP_KEEP_RECENT: "0",
    }),
  ).toThrow(/正整数/);
  expect(backupStatus(config)).toEqual({ state: "missing", healthy: false });
});

it("runs the CLI in a separate process while WAL writes continue, then checks status exit codes", async () => {
  db.exec(
    "CREATE TABLE backup_padding AS WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<2000) SELECT zeroblob(8192) payload FROM n",
  );
  const script = path.resolve(import.meta.dirname, "../tools/ops/backup.mjs");
  const env = {
    ...process.env,
    DATA_DIR: source,
    BACKUP_DIR: backups,
    BACKUP_MIN_FREE_MB: "1",
    BACKUP_MAX_TOTAL_MB: "128",
    BACKUP_MAX_SNAPSHOT_MB: "64",
  };
  const child = spawn(
    process.execPath,
    ["--max-old-space-size=64", script, "run"],
    { env, stdio: ["ignore", "pipe", "pipe"] },
  );
  let errors = "",
    writes = 0;
  child.stderr.on("data", (chunk) => {
    errors += chunk.toString();
  });
  child.stdout.resume();
  const timer = setInterval(() => {
    if (writes < 20)
      db.prepare("UPDATE submissions SET student_name=?").run(
        "revision-" + ++writes,
      );
  }, 10);
  try {
    const [code] = await once(child, "exit");
    expect(code, errors).toBe(0);
    expect(writes).toBeGreaterThan(0);
  } finally {
    clearInterval(timer);
    if (child.exitCode === null) child.kill("SIGKILL");
  }
  const state = backupStatus(config);
  expect(state.healthy).toBe(true);
  const snapshot = new DatabaseSync(
    path.join(backups, state.lastSnapshot, "smart-campus.sqlite"),
    { readOnly: true },
  );
  try {
    expect(
      snapshot.prepare("SELECT DISTINCT student_name FROM submissions").all(),
    ).toHaveLength(1);
    expect(
      snapshot.prepare("SELECT COUNT(*) n FROM backup_padding").get()!.n,
    ).toBe(2000);
  } finally {
    snapshot.close();
  }
  const status = spawn(process.execPath, [script, "status"], {
    env,
    stdio: "ignore",
  });
  expect((await once(status, "exit"))[0]).toBe(0);
  const metadata = path.join(backups, "status.json");
  fs.writeFileSync(metadata, JSON.stringify({ ...state, state: "running" }));
  expect(backupStatus(config).healthy).toBe(true);
  fs.writeFileSync(
    metadata,
    JSON.stringify({ ...state, state: "failed", error: "synthetic failure" }),
  );
  const failed = spawn(process.execPath, [script, "status"], {
    env,
    stdio: "ignore",
  });
  expect((await once(failed, "exit"))[0]).toBe(1);
}, 15000);
