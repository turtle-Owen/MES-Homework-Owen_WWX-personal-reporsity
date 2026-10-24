import { DatabaseSync, backup } from "node:sqlite";
import fs from "node:fs";
import path from "node:path";
import { createHash, randomUUID } from "node:crypto";
import { pathToFileURL } from "node:url";
import { setImmediate as yieldIO } from "node:timers/promises";

const hash = (file, checkpoint = () => {}) => {
  const digest = createHash("sha256"),
    buffer = Buffer.alloc(65536);
  const fd = fs.openSync(file, "r");
  try {
    let read;
    while ((read = fs.readSync(fd, buffer, 0, buffer.length, null)) > 0) {
      checkpoint();
      digest.update(buffer.subarray(0, read));
    }
    return digest.digest("hex");
  } finally {
    fs.closeSync(fd);
  }
};
function syncFile(file) {
  const fd = fs.openSync(file, "r");
  try {
    fs.fsyncSync(fd);
  } finally {
    fs.closeSync(fd);
  }
}
function checkDatabase(db) {
  if (
    db.prepare("PRAGMA integrity_check").get().integrity_check !== "ok" ||
    db.prepare("PRAGMA foreign_key_check").all().length
  )
    throw new Error("数据库完整性检查失败");
}
function references(db) {
  return [
    ...db.prepare("SELECT file_path FROM submissions").all(),
    ...db.prepare("SELECT file_path FROM submission_files").all(),
    ...(db
      .prepare(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='submission_tiles'",
      )
      .get()
      ? db.prepare("SELECT file_path FROM submission_tiles").all()
      : []),
  ];
}
function safeFile(root, relative) {
  if (
    typeof relative !== "string" ||
    path.isAbsolute(relative) ||
    relative.split(/[\\/]/).includes("..")
  )
    throw new Error("备份文件路径无效");
  const file = path.join(root, relative);
  const actual = fs.realpathSync(file),
    base = fs.realpathSync(root) + path.sep;
  if (!actual.startsWith(base) || !fs.statSync(actual).isFile())
    throw new Error("备份文件越界");
  return actual;
}
function publish(stage, target) {
  if (fs.existsSync(target))
    throw new Error("目标已存在；请使用新的空目录路径");
  fs.renameSync(stage, target);
  if (process.platform !== "win32") syncFile(path.dirname(target));
}
// SQLite online backup API takes a consistent database snapshot. Files are immutable
// in this app: copy only attachments referenced by that snapshot, never live WAL files.
export async function createBackup(
  source,
  destination,
  {
    ownerId,
    maxBytes = Infinity,
    minFreeBytes = 0,
    spaceCopies = 1,
    createdAt = new Date().toISOString(),
    checkpoint = () => {},
  } = {},
) {
  source = path.resolve(source);
  destination = path.resolve(destination);
  if (destination === source || destination.startsWith(source + path.sep))
    throw new Error("备份必须放在数据目录之外");
  if (fs.existsSync(destination)) throw new Error("备份目标已存在");
  fs.mkdirSync(path.dirname(destination), { recursive: true, mode: 0o700 });
  const realSource = fs.realpathSync(source);
  const realParent = fs.realpathSync(path.dirname(destination));
  if (realParent === realSource || realParent.startsWith(realSource + path.sep))
    throw new Error("备份必须放在数据目录之外");
  const stage = destination + ".partial-" + randomUUID();
  fs.mkdirSync(stage, { recursive: true, mode: 0o700 });
  let db, snapshot;
  try {
    checkpoint();
    db = new DatabaseSync(path.join(source, "smart-campus.sqlite"), {
      readOnly: true,
    });
    const pageSize = db.prepare("PRAGMA page_size").get().page_size;
    const databaseBytes =
      db.prepare("PRAGMA page_count").get().page_count * pageSize;
    const initialDisk = fs.statfsSync(stage);
    if (
      databaseBytes > maxBytes ||
      initialDisk.bavail * initialDisk.bsize < databaseBytes + minFreeBytes
    )
      throw new Error("数据库备份空间不足或超过大小上限");
    await backup(db, path.join(stage, "smart-campus.sqlite"), {
      rate: 64,
      progress: ({ totalPages }) => {
        checkpoint();
        if (totalPages * pageSize > maxBytes)
          throw new Error("数据库备份超过大小上限");
      },
    });
    db.close();
    db = null;
    snapshot = new DatabaseSync(path.join(stage, "smart-campus.sqlite"), {
      readOnly: true,
    });
    checkDatabase(snapshot);
    const rows = references(snapshot);
    const copiedDatabaseBytes = fs.statSync(
      path.join(stage, "smart-campus.sqlite"),
    ).size;
    let estimate = copiedDatabaseBytes;
    const seen = new Set();
    for (const row of rows) {
      checkpoint();
      const relative = path
        .relative(source, row.file_path)
        .split(path.sep)
        .join("/");
      if (!relative.startsWith("uploads/"))
        throw new Error("附件不在数据目录 uploads 下");
      if (!seen.has(relative)) {
        estimate += fs.statSync(safeFile(source, relative)).size;
        seen.add(relative);
      }
    }
    if (estimate > maxBytes) throw new Error("备份大小超过配置上限");
    const disk = fs.statfsSync(stage);
    if (
      disk.bavail * disk.bsize <
      estimate * spaceCopies - copiedDatabaseBytes + minFreeBytes
    )
      throw new Error("备份磁盘空间不足，保留已有备份");
    const paths = new Map();
    fs.mkdirSync(path.join(stage, "uploads"), { mode: 0o700 });
    for (const row of rows) {
      checkpoint();
      const relative = path
        .relative(source, row.file_path)
        .split(path.sep)
        .join("/");
      if (!relative.startsWith("uploads/"))
        throw new Error("附件不在数据目录 uploads 下");
      if (paths.has(relative)) continue;
      const original = safeFile(source, relative);
      await yieldIO();
      checkpoint();
      fs.mkdirSync(path.dirname(path.join(stage, relative)), {
        recursive: true,
        mode: 0o700,
      });
      fs.copyFileSync(original, path.join(stage, relative));
      fs.chmodSync(path.join(stage, relative), 0o600);
      syncFile(path.join(stage, relative));
      paths.set(relative, hash(path.join(stage, relative), checkpoint));
    }
    snapshot.close();
    snapshot = null;
    fs.chmodSync(path.join(stage, "smart-campus.sqlite"), 0o600);
    syncFile(path.join(stage, "smart-campus.sqlite"));
    paths.set(
      "smart-campus.sqlite",
      hash(path.join(stage, "smart-campus.sqlite"), checkpoint),
    );
    fs.writeFileSync(
      path.join(stage, "manifest.json"),
      JSON.stringify(
        {
          version: 1,
          ...(ownerId ? { ownerId } : {}),
          createdAt,
          source,
          files: Object.fromEntries(paths),
        },
        null,
        2,
      ),
      { mode: 0o600 },
    );
    syncFile(path.join(stage, "manifest.json"));
    if (process.platform !== "win32") {
      // Sync each nested attachment directory before publishing the root.
      const dirs = new Set([stage, path.join(stage, "uploads")]);
      for (const relative of paths.keys()) {
        let dir = path.dirname(path.join(stage, relative));
        while (dir !== stage && dir.startsWith(stage + path.sep)) {
          dirs.add(dir);
          dir = path.dirname(dir);
        }
      }
      for (const dir of [...dirs].sort((a, b) => b.length - a.length))
        syncFile(dir);
    }
    publish(stage, destination);
    return { files: paths.size };
  } catch (e) {
    db?.close();
    db = null;
    snapshot?.close();
    snapshot = null;
    fs.rmSync(stage, { recursive: true, force: true });
    throw e;
  } finally {
    db?.close();
    snapshot?.close();
  }
}
export function verifyBackup(source, { checkpoint = () => {} } = {}) {
  checkpoint();
  source = path.resolve(source);
  const wal = path.join(source, "smart-campus.sqlite-wal");
  if (fs.existsSync(wal) && fs.statSync(wal).size > 0)
    throw new Error("备份目录含未登记的 WAL，拒绝校验或恢复");
  if (fs.statSync(safeFile(source, "manifest.json")).size > 32 * 1024 * 1024)
    throw new Error("备份清单过大");
  const manifest = JSON.parse(
    fs.readFileSync(safeFile(source, "manifest.json"), "utf8"),
  );
  if (
    manifest.version !== 1 ||
    !manifest.files ||
    Array.isArray(manifest.files) ||
    typeof manifest.source !== "string" ||
    !path.isAbsolute(manifest.source) ||
    !manifest.files["smart-campus.sqlite"]
  )
    throw new Error("备份清单无效");
  let bytes = 0;
  for (const [relative, expected] of Object.entries(manifest.files)) {
    checkpoint();
    if (relative !== "smart-campus.sqlite" && !relative.startsWith("uploads/"))
      throw new Error("备份包含未知文件");
    if (
      typeof expected !== "string" ||
      !/^[a-f0-9]{64}$/.test(expected) ||
      hash(safeFile(source, relative), checkpoint) !== expected
    )
      throw new Error("备份文件校验失败");
    bytes += fs.statSync(safeFile(source, relative)).size;
  }
  const db = new DatabaseSync(safeFile(source, "smart-campus.sqlite"), {
    readOnly: true,
  });
  try {
    checkDatabase(db);
    for (const row of references(db)) {
      const relative = path
        .relative(manifest.source, row.file_path)
        .split(path.sep)
        .join("/");
      if (!relative.startsWith("uploads/") || !manifest.files[relative])
        throw new Error("附件备份不完整");
    }
  } finally {
    db.close();
  }
  return { manifest, bytes, files: Object.keys(manifest.files).length };
}
export function restoreBackup(
  source,
  destination,
  { checkpoint = () => {} } = {},
) {
  checkpoint();
  source = path.resolve(source);
  destination = path.resolve(destination);
  if (fs.existsSync(destination))
    throw new Error("恢复目标已存在；禁止覆盖现有数据");
  const { manifest } = verifyBackup(source, { checkpoint });
  const stage = destination + ".partial-" + randomUUID();
  fs.mkdirSync(stage, { recursive: true, mode: 0o700 });
  let db;
  try {
    fs.mkdirSync(path.join(stage, "uploads"), { mode: 0o700 });
    for (const relative of Object.keys(manifest.files)) {
      checkpoint();
      const target = path.join(stage, relative);
      fs.mkdirSync(path.dirname(target), { recursive: true, mode: 0o700 });
      fs.copyFileSync(safeFile(source, relative), target);
      fs.chmodSync(target, 0o600);
    }
    db = new DatabaseSync(path.join(stage, "smart-campus.sqlite"));
    checkDatabase(db);
    if (
      db
        .prepare("SELECT 1 FROM sqlite_master WHERE name='submission_tiles'")
        .get()
    )
      db.exec(
        "CREATE INDEX IF NOT EXISTS idx_tiles_file_path ON submission_tiles(file_path)",
      );
    db.exec("BEGIN IMMEDIATE");
    const moved = new Set();
    for (const row of references(db)) {
      checkpoint();
      if (moved.has(row.file_path)) continue;
      moved.add(row.file_path);
      const relative = path
        .relative(manifest.source, row.file_path)
        .split(path.sep)
        .join("/");
      if (!relative.startsWith("uploads/") || !manifest.files[relative])
        throw new Error("附件备份不完整");
      for (const table of [
        "submissions",
        "submission_files",
        ...(db
          .prepare("SELECT 1 FROM sqlite_master WHERE name='submission_tiles'")
          .get()
          ? ["submission_tiles"]
          : []),
      ])
        db.prepare(`UPDATE ${table} SET file_path=? WHERE file_path=?`).run(
          path.join(destination, relative),
          row.file_path,
        );
    }
    // Sessions from a backup must never become valid again; old leases cannot survive restoration.
    db.exec(
      "DELETE FROM sessions; UPDATE model_usage SET finished_at=COALESCE(finished_at,0),lease_until=0; UPDATE recognition_jobs SET lease_until=0 WHERE status='running'; COMMIT",
    );
    if (
      db
        .prepare("SELECT 1 FROM sqlite_master WHERE name='upload_sessions'")
        .get()
    ) {
      db.exec(
        "UPDATE upload_sessions SET status='cancelled' WHERE status='open'; DELETE FROM upload_parts",
      );
      if (
        db
          .prepare("SELECT 1 FROM sqlite_master WHERE name='upload_writers'")
          .get()
      )
        db.exec("DELETE FROM upload_writers");
      if (
        db
          .prepare(
            "SELECT 1 FROM sqlite_master WHERE name='upload_temporary_files'",
          )
          .get()
      )
        db.exec("DELETE FROM upload_temporary_files");
    }
    if (
      db
        .prepare("SELECT 1 FROM sqlite_master WHERE name='provider_files'")
        .get()
    ) {
      db.exec(
        "UPDATE provider_files SET status='cleanup',cleanup_after=0 WHERE remote_id IS NOT NULL AND status NOT IN('deleted','expired')",
      );
    }
    db.exec(
      "CREATE TABLE IF NOT EXISTS system_flags(key TEXT PRIMARY KEY,value TEXT NOT NULL); INSERT OR REPLACE INTO system_flags VALUES('restored_budget_review','1')",
    );
    checkDatabase(db);
    db.close();
    db = null;
    publish(stage, destination);
  } catch (e) {
    db?.close();
    db = null;
    fs.rmSync(stage, { recursive: true, force: true });
    throw e;
  } finally {
    db?.close();
  }
}
if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href
) {
  const [command, source, destination] = process.argv.slice(2);
  try {
    if (
      !source ||
      (command !== "verify" && !destination) ||
      !["backup", "restore", "verify"].includes(command)
    )
      throw new Error(
        "用法：node tools/ops/storage.mjs <backup|restore> <源目录> <新目标目录>；或 verify <备份目录>",
      );
    if (command === "backup") await createBackup(source, destination);
    else if (command === "restore") restoreBackup(source, destination);
    else verifyBackup(source);
    console.log(
      command === "backup"
        ? "备份完成，已校验数据库与附件"
        : command === "verify"
          ? "备份校验通过"
          : "恢复完成；使用原 APP_SECRET。启用金额预算时，模型调用需先对账确认",
    );
  } catch (e) {
    console.error(e.message);
    process.exitCode = 1;
  }
}
