import { DatabaseSync, backup } from "node:sqlite";
import fs from "node:fs";
import path from "node:path";
import { createHash, randomUUID } from "node:crypto";
import { pathToFileURL } from "node:url";

const hash = (file) => {
  const digest = createHash("sha256"),
    buffer = Buffer.alloc(65536);
  const fd = fs.openSync(file, "r");
  try {
    let read;
    while ((read = fs.readSync(fd, buffer, 0, buffer.length, null)) > 0)
      digest.update(buffer.subarray(0, read));
    return digest.digest("hex");
  } finally {
    fs.closeSync(fd);
  }
};
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
}
// SQLite online backup API takes a consistent database snapshot. Files are immutable
// in this app: copy only attachments referenced by that snapshot, never live WAL files.
export async function createBackup(source, destination) {
  source = path.resolve(source);
  destination = path.resolve(destination);
  if (destination === source || destination.startsWith(source + path.sep))
    throw new Error("备份必须放在数据目录之外");
  if (fs.existsSync(destination)) throw new Error("备份目标已存在");
  const stage = destination + ".partial-" + randomUUID();
  fs.mkdirSync(stage, { recursive: true, mode: 0o700 });
  let db, snapshot;
  try {
    db = new DatabaseSync(path.join(source, "smart-campus.sqlite"), {
      readOnly: true,
    });
    await backup(db, path.join(stage, "smart-campus.sqlite"));
    db.close();
    db = null;
    snapshot = new DatabaseSync(path.join(stage, "smart-campus.sqlite"), {
      readOnly: true,
    });
    checkDatabase(snapshot);
    const paths = new Map();
    fs.mkdirSync(path.join(stage, "uploads"), { mode: 0o700 });
    for (const row of references(snapshot)) {
      const relative = path
        .relative(source, row.file_path)
        .split(path.sep)
        .join("/");
      if (!relative.startsWith("uploads/"))
        throw new Error("附件不在数据目录 uploads 下");
      if (paths.has(relative)) continue;
      const original = safeFile(source, relative);
      fs.mkdirSync(path.dirname(path.join(stage, relative)), {
        recursive: true,
        mode: 0o700,
      });
      fs.copyFileSync(original, path.join(stage, relative));
      fs.chmodSync(path.join(stage, relative), 0o600);
      paths.set(relative, hash(path.join(stage, relative)));
    }
    snapshot.close();
    snapshot = null;
    fs.chmodSync(path.join(stage, "smart-campus.sqlite"), 0o600);
    paths.set(
      "smart-campus.sqlite",
      hash(path.join(stage, "smart-campus.sqlite")),
    );
    fs.writeFileSync(
      path.join(stage, "manifest.json"),
      JSON.stringify(
        {
          version: 1,
          createdAt: new Date().toISOString(),
          source,
          files: Object.fromEntries(paths),
        },
        null,
        2,
      ),
      { mode: 0o600 },
    );
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
export function restoreBackup(source, destination) {
  source = path.resolve(source);
  destination = path.resolve(destination);
  if (fs.existsSync(destination))
    throw new Error("恢复目标已存在；禁止覆盖现有数据");
  const manifest = JSON.parse(
    fs.readFileSync(safeFile(source, "manifest.json"), "utf8"),
  );
  if (
    manifest.version !== 1 ||
    !manifest.files ||
    !path.isAbsolute(manifest.source) ||
    !manifest.files["smart-campus.sqlite"]
  )
    throw new Error("备份清单无效");
  for (const [relative, expected] of Object.entries(manifest.files)) {
    if (relative !== "smart-campus.sqlite" && !relative.startsWith("uploads/"))
      throw new Error("备份包含未知文件");
    if (hash(safeFile(source, relative)) !== expected)
      throw new Error("备份文件校验失败");
  }
  const stage = destination + ".partial-" + randomUUID();
  fs.mkdirSync(stage, { recursive: true, mode: 0o700 });
  let db;
  try {
    fs.mkdirSync(path.join(stage, "uploads"), { mode: 0o700 });
    for (const relative of Object.keys(manifest.files)) {
      const target = path.join(stage, relative);
      fs.mkdirSync(path.dirname(target), { recursive: true, mode: 0o700 });
      fs.copyFileSync(safeFile(source, relative), target);
      fs.chmodSync(target, 0o600);
    }
    db = new DatabaseSync(path.join(stage, "smart-campus.sqlite"));
    checkDatabase(db);
    db.exec("BEGIN IMMEDIATE");
    for (const row of references(db)) {
      const relative = path
        .relative(manifest.source, row.file_path)
        .split(path.sep)
        .join("/");
      if (!relative.startsWith("uploads/") || !manifest.files[relative])
        throw new Error("附件备份不完整");
      for (const table of ["submissions", "submission_files"])
        db.prepare(`UPDATE ${table} SET file_path=? WHERE file_path=?`).run(
          path.join(destination, relative),
          row.file_path,
        );
    }
    // Sessions from a backup must never become valid again; old leases cannot survive restoration.
    db.exec(
      "DELETE FROM sessions; UPDATE model_usage SET finished_at=COALESCE(finished_at,0),lease_until=0; UPDATE recognition_jobs SET lease_until=0 WHERE status='running'; COMMIT",
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
    if (!source || !destination || !["backup", "restore"].includes(command))
      throw new Error(
        "用法：node tools/ops/storage.mjs <backup|restore> <源目录> <新目标目录>",
      );
    if (command === "backup") await createBackup(source, destination);
    else restoreBackup(source, destination);
    console.log(
      command === "backup"
        ? "备份完成，已校验数据库与附件"
        : "恢复完成；请使用原 APP_SECRET 并检查后启动服务",
    );
  } catch (e) {
    console.error(e.message);
    process.exitCode = 1;
  }
}
