import fs from "node:fs";
import path from "node:path";
import { Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { watchUpload, storageStatus } from "./io-guards.mjs";
import { randomUUID, createHash } from "node:crypto";
import { tilePlan } from "../shared/tile-plan.mjs";
import { imageHeader } from "./image-header.mjs";
import { uploadError, positive } from "./upload-admission.mjs";
const ORIGINAL_MAX = 20 * 1024 * 1024,
  TILE_MAX = 2 * 1024 * 1024;
export function registerTiledUploads(
  app,
  { db, auth, accessibleAssignment, files, data, admission, costBudget },
) {
  const enabled = process.env.CLIENT_TILING_ENABLED === "1" && costBudget.ready;
  const maxTiles = Math.min(positive("UPLOAD_MAX_TILES", 24), 48);
  const load = (id, user) => {
    const s = db
      .prepare(
        "SELECT * FROM upload_sessions WHERE id=? AND user_id=? AND school_id=? AND assignment_id IS NOT NULL",
      )
      .get(id, user.id, user.school_id);
    if (!s) throw uploadError("上传会话不存在", 404);
    return s;
  };
  const assignmentFor = (s, user) => {
    const a = accessibleAssignment({ user, params: { id: s.assignment_id } });
    if (!a || user.is_school_admin) throw uploadError("作业不存在", 404);
    if (s.student_id) {
      const student = db
        .prepare(
          "SELECT * FROM users WHERE id=? AND school_id=? AND role='student'",
        )
        .get(s.student_id, user.school_id);
      if (
        !student ||
        student.stage !== a.stage ||
        student.class_name !== a.class_name
      )
        throw uploadError("学生不属于作业班级", 403);
    }
    return a;
  };
  const open = (s) => {
    if (s.status !== "open" || s.expires_at <= Date.now())
      throw uploadError("上传会话已关闭或过期", 409);
  };
  const reply = (res, e) =>
    res.status(e.status || 400).json({ error: e.message });
  const describe = (s) => ({
    id: s.id,
    status: s.status,
    expiresAt: s.expires_at,
    maxBytes: s.reserved_bytes,
    parts: JSON.parse(s.plan_json),
    received: db
      .prepare(
        "SELECT index_no,sha256,bytes FROM upload_parts WHERE session_id=? AND status='received' ORDER BY index_no",
      )
      .all(s.id),
    submissionId: s.submission_id,
  });
  app.get("/api/upload-capabilities", auth, (_req, res) =>
    res.json({
      clientTiling: enabled,
      maxTiles,
      maxPages: 10,
      maxPixels: 24_000_000,
    }),
  );
  app.post("/api/assignments/:id/upload-sessions", auth, (req, res) => {
    try {
      if (!enabled)
        throw uploadError("设备端切片尚未启用或费用预算未配置", 409);
      const a = accessibleAssignment(req);
      if (!a || req.user.is_school_admin) throw uploadError("作业不存在", 404);
      const studentId =
        req.user.role === "student" ? req.user.id : req.body.studentId || null;
      assignmentFor({ assignment_id: a.id, student_id: studentId }, req.user);
      if (
        !Array.isArray(req.body.pages) ||
        req.body.pages.length < 1 ||
        req.body.pages.length > 10
      )
        throw uploadError("照片页数须为 1–10 页");
      let plan = [],
        tiles = 0;
      for (const [page, p] of req.body.pages.entries()) {
        const grid = tilePlan(p.width, p.height);
        tiles += grid.length;
        if (tiles > maxTiles)
          throw uploadError("切片数量超过上限，请减少照片或分次拍摄");
        const name = String(p.name || `第${page + 1}页`)
          .replace(/[\x00-\x1f]/g, "")
          .slice(0, 200);
        plan.push({
          index: plan.length,
          kind: "original",
          page,
          width: p.width,
          height: p.height,
          name,
          maxBytes:
            Number.isSafeInteger(p.bytes) &&
            p.bytes > 0 &&
            p.bytes <= ORIGINAL_MAX
              ? p.bytes
              : ORIGINAL_MAX,
        });
        for (const tile of grid)
          plan.push({
            index: plan.length,
            kind: "tile",
            page,
            ...tile,
            maxBytes: TILE_MAX,
          });
      }
      const session = admission.reserve(req.user, {
        assignmentId: a.id,
        studentId,
        plan,
        bytes: Math.min(
          admission.maxBytes,
          plan.reduce((sum, part) => sum + part.maxBytes, 0),
        ),
      });
      const ins = db.prepare(
        "INSERT INTO upload_parts(session_id,index_no) VALUES(?,?)",
      );
      // A session without all part rows cannot finalize; recoverable by cancellation.
      db.exec("BEGIN IMMEDIATE");
      try {
        for (const part of plan) ins.run(session.id, part.index);
        db.exec("COMMIT");
      } catch (e) {
        db.exec("ROLLBACK");
        throw e;
      }
      res.status(201).json(describe(session));
    } catch (e) {
      reply(res, e);
    }
  });
  app.get("/api/upload-sessions/:id", auth, (req, res) => {
    try {
      res.json(describe(load(req.params.id, req.user)));
    } catch (e) {
      reply(res, e);
    }
  });
  app.delete("/api/upload-sessions/:id", auth, (req, res) => {
    try {
      const s = load(req.params.id, req.user);
      if (s.status === "complete")
        throw uploadError("已完成的提交不可取消", 409);
      db.prepare(
        "UPDATE upload_sessions SET status='cancelled' WHERE id=?",
      ).run(s.id);
      res.json({ ok: true });
    } catch (e) {
      reply(res, e);
    }
  });
  app.put("/api/upload-sessions/:id/parts/:index", auth, async (req, res) => {
    let release, temp, token, session, index;
    try {
      session = load(req.params.id, req.user);
      open(session);
      assignmentFor(session, req.user);
      index = Number(req.params.index);
      const part = JSON.parse(session.plan_json)[index];
      if (!Number.isSafeInteger(index) || !part || part.index !== index)
        throw uploadError("切片序号无效");
      const digest = req.get("x-content-sha256");
      if (!/^[a-f0-9]{64}$/.test(digest || ""))
        throw uploadError("需要完整 SHA-256");
      const declaredLength = req.get("content-length");
      const length =
        declaredLength === undefined ? part.maxBytes : Number(declaredLength);
      if (
        !Number.isSafeInteger(length) ||
        length <= 0 ||
        length > part.maxBytes
      )
        throw uploadError("文件大小无效或超过上限", 413);
      release = admission.acquire(req.user, req.ip);
      token = randomUUID();
      const previous = db
        .prepare("SELECT * FROM upload_parts WHERE session_id=? AND index_no=?")
        .get(session.id, index);
      if (previous.status === "received") {
        if (
          previous.sha256 !== digest ||
          (declaredLength !== undefined && previous.bytes !== length)
        )
          throw uploadError("同一序号已上传不同内容", 409);
        res
          .set("Connection", "close")
          .json({ ok: true, alreadyReceived: true });
        return;
      }
      const declaredMime = req.get("content-type")?.split(";")[0];
      const extensions = {
        "image/jpeg": ".jpg",
        "image/png": ".png",
        "image/webp": ".webp",
      };
      if (!Object.hasOwn(extensions, declaredMime || ""))
        throw uploadError("仅支持 JPEG/PNG/WebP");
      // Persist the final random path before writing so crash cleanup can find it.
      temp = path.join(files, randomUUID() + extensions[declaredMime]);
      db.exec("BEGIN IMMEDIATE");
      try {
        open(load(session.id, req.user));
        const used = db
          .prepare(
            "SELECT COALESCE(SUM(bytes),0) n FROM upload_parts WHERE session_id=? AND (status='received' OR status='receiving')",
          )
          .get(session.id).n;
        if (used + length > session.reserved_bytes)
          throw uploadError("会话累计字节数超限", 413);
        admission.chargeTransfer(req.user, length, false);
        const update = db
          .prepare(
            "UPDATE upload_parts SET status='receiving',bytes=?,token=?,lease_until=?,file_path=? WHERE session_id=? AND index_no=? AND (status='empty' OR status='receiving' AND lease_until<=?)",
          )
          .run(
            length,
            token,
            Date.now() + 65000,
            temp,
            session.id,
            index,
            Date.now(),
          );
        if (!update.changes) throw uploadError("此切片正在上传", 409);
        db.exec("COMMIT");
      } catch (e) {
        db.exec("ROLLBACK");
        throw e;
      }
      const hash = createHash("sha256"),
        header = [];
      let total = 0,
        headerBytes = 0;
      const inspect = new Transform({
        transform(chunk, _encoding, cb) {
          total += chunk.length;
          if (total > length || total > part.maxBytes)
            return cb(uploadError("文件实际大小超限", 413));
          hash.update(chunk);
          if (headerBytes < 65536) {
            const b = Buffer.from(chunk.subarray(0, 65536 - headerBytes));
            header.push(b);
            headerBytes += b.length;
          }
          cb(null, chunk);
        },
      });
      const output = fs.createWriteStream(temp, { flags: "wx", mode: 0o600 });
      const closed = new Promise((resolve) => output.once("close", resolve));
      const watch = watchUpload(req, {
        maxBytes: length,
        totalMs: admission.totalMs,
        idleMs: admission.idleMs,
      });
      try {
        await pipeline(req, inspect, output, { signal: watch.signal });
      } finally {
        watch.stop();
        await closed;
      }
      if (
        (declaredLength !== undefined && total !== length) ||
        total === 0 ||
        hash.digest("hex") !== digest
      )
        throw uploadError("文件大小或哈希不匹配");
      const info = imageHeader(Buffer.concat(header));
      // EXIF orientation can swap the displayed axes. Tiles themselves have no EXIF rotation.
      const dims = info.width === part.width && info.height === part.height;
      const rotated =
        part.kind === "original" &&
        info.height === part.width &&
        info.width === part.height;
      if (!dims && !rotated) throw uploadError("图片头部尺寸与切片计划不一致");
      if (part.kind === "tile" && info.mime !== "image/jpeg")
        throw uploadError("切片须编码为 JPEG");
      if (info.mime !== declaredMime)
        throw uploadError("实际图片格式与声明不一致");
      const target = temp;
      db.exec("BEGIN IMMEDIATE");
      try {
        open(load(session.id, req.user));
        const changed = db
          .prepare(
            "UPDATE upload_parts SET status='received',sha256=?,mime=?,file_path=?,bytes=?,token=NULL,lease_until=NULL WHERE session_id=? AND index_no=? AND token=?",
          )
          .run(digest, info.mime, target, total, session.id, index, token);
        if (!changed.changes) throw uploadError("上传租约失效", 409);
        db.exec("COMMIT");
      } catch (e) {
        db.exec("ROLLBACK");
        throw e;
      }
      temp = null;
      res.json({ ok: true });
    } catch (e) {
      let removed = true;
      if (temp) {
        try {
          await fs.promises.rm(temp, { force: true });
        } catch {
          removed = false;
        } // Keep the registered path for janitor retry.
      }
      if (token && session && removed)
        db.prepare(
          "UPDATE upload_parts SET status='empty',file_path=NULL,bytes=NULL,token=NULL,lease_until=NULL WHERE session_id=? AND index_no=? AND token=?",
        ).run(session.id, index, token);
      if (!res.destroyed)
        res
          .set("Connection", "close")
          .set("Retry-After", "5")
          .status(storageStatus(e) || e.status || 400)
          .json({ error: e.status ? e.message : "上传失败，请重试" });
    } finally {
      release?.();
    }
  });
  app.post("/api/upload-sessions/:id/finalize", auth, (req, res) => {
    try {
      const session = load(req.params.id, req.user);
      assignmentFor(session, req.user);
      if (session.status === "complete")
        return res.json({ id: session.submission_id });
      open(session);
      db.exec("BEGIN IMMEDIATE");
      try {
        const current = load(session.id, req.user);
        open(current);
        const plan = JSON.parse(current.plan_json),
          parts = db
            .prepare(
              "SELECT * FROM upload_parts WHERE session_id=? ORDER BY index_no",
            )
            .all(current.id);
        if (
          parts.length !== plan.length ||
          parts.some((p, i) => p.index_no !== i || p.status !== "received")
        )
          throw uploadError("切片尚未全部上传，不能创建提交", 409);
        const bytes = parts.reduce((sum, p) => sum + p.bytes, 0);
        if (bytes > current.reserved_bytes)
          throw uploadError("会话总大小超限", 413);
        const student =
          current.student_id &&
          db
            .prepare("SELECT name FROM users WHERE id=?")
            .get(current.student_id);
        const id = randomUUID(),
          originals = plan.filter((p) => p.kind === "original");
        const first = parts[originals[0].index];
        db.prepare(
          "INSERT INTO submissions(id,assignment_id,student_id,student_name,file_path,file_type,status,created_at) VALUES(?,?,?,?,?,?,'uploaded',?)",
        ).run(
          id,
          current.assignment_id,
          current.student_id,
          student?.name || req.user.name,
          first.file_path,
          first.mime,
          new Date().toISOString(),
        );
        for (const [i, p] of originals.entries()) {
          const f = parts[p.index];
          db.prepare("INSERT INTO submission_files VALUES(?,?,?,?,?,?)").run(
            randomUUID(),
            id,
            i,
            f.file_path,
            f.mime,
            p.name,
          );
        }
        for (const [i, p] of plan.filter((p) => p.kind === "tile").entries()) {
          const f = parts[p.index];
          db.prepare(
            "INSERT INTO submission_tiles VALUES(?,?,?,?,?,?,?,?,?)",
          ).run(
            id,
            i,
            p.page,
            p.x,
            p.y,
            p.width,
            p.height,
            f.file_path,
            f.mime,
          );
        }
        db.prepare(
          "UPDATE upload_sessions SET status='complete',submission_id=?,actual_bytes=? WHERE id=?",
        ).run(id, bytes, current.id);
        db.prepare("DELETE FROM upload_parts WHERE session_id=?").run(
          current.id,
        );
        db.exec("COMMIT");
        res.json({ id });
      } catch (e) {
        db.exec("ROLLBACK");
        throw e;
      }
    } catch (e) {
      reply(res, e);
    }
  });
  // Bounded cleanup. Only paths created by this module are eligible for removal.
  let cleaning = false;
  async function clean() {
    if (cleaning) return;
    cleaning = true;
    try {
      const time = Date.now();
      db.prepare(
        "UPDATE upload_sessions SET status='expired' WHERE status='open' AND expires_at<=?",
      ).run(time);
      db.prepare(
        "DELETE FROM upload_sessions WHERE status IN('complete','cancelled','expired') AND created_at<? AND NOT EXISTS(SELECT 1 FROM upload_parts WHERE session_id=upload_sessions.id)",
      ).run(time - 7 * 86400000);
      const stale = db
        .prepare(
          `SELECT p.* FROM upload_parts p JOIN upload_sessions s ON s.id=p.session_id
      WHERE s.status IN('expired','cancelled') OR p.status='receiving' AND p.lease_until<=? LIMIT 50`,
        )
        .all(time);
      for (const p of stale) {
        if (p.file_path && path.dirname(p.file_path) === files)
          await fs.promises.rm(p.file_path, { force: true });
        db.prepare(
          "UPDATE upload_parts SET status='empty',file_path=NULL,bytes=NULL,token=NULL,lease_until=NULL WHERE session_id=? AND index_no=? AND status=? AND token IS ?",
        ).run(p.session_id, p.index_no, p.status, p.token);
        db.prepare(
          "DELETE FROM upload_parts WHERE session_id=? AND index_no=? AND EXISTS(SELECT 1 FROM upload_sessions WHERE id=? AND status IN('expired','cancelled'))",
        ).run(p.session_id, p.index_no, p.session_id);
      }
    } finally {
      cleaning = false;
    }
  }
  const timer = setInterval(
    () => void clean().catch(() => console.error("Upload cleanup failed")),
    30000,
  );
  timer.unref();
  void clean().catch(() => console.error("Upload cleanup failed"));
  return {
    clean,
    stop() {
      clearInterval(timer);
    },
  };
}
