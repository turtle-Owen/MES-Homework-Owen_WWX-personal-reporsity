import fs from "node:fs";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { pipeline } from "node:stream/promises";

// Multer's default disk storage does not cancel its writer on request abort.
// Register paths before opening them, and retain a lease until actual close.
export function createUploadStorage(db, directory) {
  let cleaning = false;
  const remove = async (target) => {
    if (path.dirname(target) !== directory)
      throw new Error("Invalid upload path");
    await fs.promises.rm(target, { force: true });
    db.prepare("DELETE FROM upload_temporary_files WHERE file_path=?").run(
      target,
    );
  };
  async function clean() {
    if (cleaning) return;
    cleaning = true;
    try {
      const rows = db
        .prepare(
          "SELECT file_path FROM upload_temporary_files WHERE expires_at<=? LIMIT 20",
        )
        .all(Date.now());
      for (const row of rows) await remove(row.file_path);
    } finally {
      cleaning = false;
    }
  }
  const timer = setInterval(
    () =>
      void clean().catch(() =>
        console.error("Temporary upload cleanup failed"),
      ),
    30000,
  );
  timer.unref();
  return {
    _handleFile(req, file, cb) {
      const target = path.join(directory, randomUUID());
      try {
        db.prepare("INSERT INTO upload_temporary_files VALUES(?,?)").run(
          target,
          Date.now() + 600000,
        );
      } catch (error) {
        cb(error);
        return;
      }
      req.uploadTempPaths ||= new Set();
      req.uploadTempPaths.add(target);
      const output = fs.createWriteStream(target, { flags: "wx", mode: 0o600 });
      const closed = new Promise((resolve) => output.once("close", resolve));
      req.uploadTasks ||= new Set();
      req.uploadTasks.add(closed);
      closed.finally(() => req.uploadTasks.delete(closed));
      pipeline(file.stream, output, { signal: req.uploadWatch?.signal }).then(
        () =>
          cb(null, {
            destination: directory,
            filename: path.basename(target),
            path: target,
            size: output.bytesWritten,
          }),
        (error) => {
          void remove(target).catch(() => {});
          cb(error);
        },
      );
    },
    _removeFile(_req, file, cb) {
      remove(file.path).then(() => cb(null), cb);
    },
    async cleanupRequest(req) {
      await Promise.allSettled([...(req.uploadTasks || [])]);
      for (const target of req.uploadTempPaths || []) await remove(target);
    },
    clean,
    stop() {
      clearInterval(timer);
    },
  };
}
