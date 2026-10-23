import fs from "node:fs";
import { randomUUID } from "node:crypto";
export const uploadError = (message, status = 400) =>
  Object.assign(new Error(message), { status });
export const positive = (name, fallback) => {
  const n = Number(process.env[name]);
  return Number.isSafeInteger(n) && n > 0 ? n : fallback;
};
export function createUploadAdmission(db, dataDir) {
  const maxBytes = positive("UPLOAD_TOTAL_MB", 50) * 1024 * 1024;
  const globalScratch = positive("UPLOAD_SCRATCH_MB", 512) * 1024 * 1024;
  const schoolScratch = positive("UPLOAD_SCHOOL_SCRATCH_MB", 128) * 1024 * 1024;
  const globalConcurrent = positive("UPLOAD_CONCURRENCY", 4);
  const ipWindows = new Map();
  function ipLimit(ip) {
    const time = Date.now(),
      value = ipWindows.get(ip);
    const current =
      value && value.until > time ? value : { until: time + 60000, count: 0 };
    if (++current.count > positive("UPLOAD_IP_REQUESTS_PER_MINUTE", 600))
      throw uploadError("上传请求过于频繁，请稍后重试", 429);
    ipWindows.delete(ip);
    ipWindows.set(ip, current);
    if (ipWindows.size > 2000) ipWindows.delete(ipWindows.keys().next().value);
  }
  function reserve(
    user,
    {
      assignmentId = null,
      studentId = null,
      plan = [],
      ttl = 600000,
      bytes = maxBytes,
    } = {},
  ) {
    if (!Number.isSafeInteger(bytes) || bytes <= 0 || bytes > maxBytes)
      throw uploadError("上传总大小超过上限", 413);
    const time = Date.now(),
      id = randomUUID();
    db.exec("BEGIN IMMEDIATE");
    try {
      const usage = db
        .prepare(
          `SELECT COUNT(*) count,
        COALESCE(SUM(CASE WHEN status='open' AND expires_at>? THEN reserved_bytes ELSE 0 END),0) reserved,
        COALESCE(SUM(CASE WHEN school_id=? AND status='open' AND expires_at>? THEN reserved_bytes ELSE 0 END),0) school_reserved,
        COALESCE(SUM(CASE WHEN user_id=? AND status='open' AND expires_at>? THEN 1 ELSE 0 END),0) user_open,
        COALESCE(SUM(CASE WHEN user_id=? AND created_at>? THEN 1 ELSE 0 END),0) user_day,
        COALESCE(SUM(CASE WHEN school_id=? AND created_at>? THEN 1 ELSE 0 END),0) school_day
        FROM upload_sessions WHERE status='open' OR created_at>?`,
        )
        .get(
          time,
          user.school_id,
          time,
          user.id,
          time,
          user.id,
          time - 86400000,
          user.school_id,
          time - 86400000,
          time - 86400000,
        );
      if (
        usage.count >= positive("UPLOAD_GLOBAL_DAILY_LIMIT", 10000) ||
        usage.user_open >= 2 ||
        usage.user_day >= positive("UPLOAD_USER_DAILY_LIMIT", 30) ||
        usage.school_day >= positive("UPLOAD_SCHOOL_DAILY_LIMIT", 500)
      )
        throw uploadError("上传会话配额已满，请稍后重试", 429);
      if (
        usage.reserved + bytes > globalScratch ||
        usage.school_reserved + bytes > schoolScratch
      )
        throw uploadError("上传暂存空间繁忙，请稍后重试", 429);
      const disk = fs.statfsSync(dataDir);
      if (disk.bavail * disk.bsize < usage.reserved + bytes + 128 * 1024 * 1024)
        throw uploadError("服务器可用空间不足，暂不能接收文件", 507);
      db.prepare(
        "INSERT INTO upload_sessions VALUES(?,?,?,?,?,'open',?,?,0,?,?,NULL)",
      ).run(
        id,
        user.id,
        user.school_id,
        assignmentId,
        studentId,
        JSON.stringify(plan),
        bytes,
        time,
        time + ttl,
      );
      db.exec("COMMIT");
      return db.prepare("SELECT * FROM upload_sessions WHERE id=?").get(id);
    } catch (e) {
      db.exec("ROLLBACK");
      throw e;
    }
  }
  // Persistent ingress leases include legacy multipart requests. They bound active
  // writers and recover after a process crash without trusting forwarded IP headers.
  db.exec(
    "CREATE TABLE IF NOT EXISTS upload_writers(id TEXT PRIMARY KEY,user_id TEXT,expires_at INTEGER NOT NULL)",
  );
  function acquire(user, ip) {
    ipLimit(ip);
    const time = Date.now(),
      id = randomUUID();
    db.exec("BEGIN IMMEDIATE");
    try {
      db.prepare("DELETE FROM upload_writers WHERE expires_at<=?").run(time);
      const count = db
        .prepare(
          "SELECT COUNT(*) total,COALESCE(SUM(user_id=?),0) own FROM upload_writers",
        )
        .get(user.id);
      if (count.total >= globalConcurrent || count.own >= 1)
        throw uploadError("正在上传其他文件，请稍后重试", 429);
      db.prepare("INSERT INTO upload_writers VALUES(?,?,?)").run(
        id,
        user.id,
        time + 90000,
      );
      db.exec("COMMIT");
    } catch (e) {
      db.exec("ROLLBACK");
      throw e;
    }
    return () => db.prepare("DELETE FROM upload_writers WHERE id=?").run(id);
  }
  function chargeTransfer(user, bytes, ownTransaction = true) {
    const time = Date.now();
    if (ownTransaction) db.exec("BEGIN IMMEDIATE");
    try {
      db.prepare("DELETE FROM upload_attempts WHERE created_at<=?").run(
        time - 86400000,
      );
      const usage = db
        .prepare(
          `SELECT COUNT(*) total_hour,
        COALESCE(SUM(CASE WHEN user_id=? THEN 1 ELSE 0 END),0) user_hour,
        COALESCE(SUM(CASE WHEN school_id=? THEN 1 ELSE 0 END),0) school_hour
        FROM upload_attempts WHERE created_at>?`,
        )
        .get(user.id, user.school_id, time - 3600000);
      const daily = db
        .prepare(
          `SELECT COALESCE(SUM(bytes_reserved),0) total,
        COALESCE(SUM(CASE WHEN user_id=? THEN bytes_reserved ELSE 0 END),0) own,
        COALESCE(SUM(CASE WHEN school_id=? THEN bytes_reserved ELSE 0 END),0) school FROM upload_attempts`,
        )
        .get(user.id, user.school_id);
      if (
        usage.total_hour >= positive("UPLOAD_GLOBAL_HOURLY_PARTS", 6000) ||
        usage.user_hour >= positive("UPLOAD_USER_HOURLY_PARTS", 180) ||
        usage.school_hour >= positive("UPLOAD_SCHOOL_HOURLY_PARTS", 3000) ||
        daily.total + bytes >
          positive("UPLOAD_GLOBAL_DAILY_MB", 8192) * 1024 * 1024 ||
        daily.own + bytes >
          positive("UPLOAD_USER_DAILY_MB", 300) * 1024 * 1024 ||
        daily.school + bytes >
          positive("UPLOAD_SCHOOL_DAILY_MB", 3072) * 1024 * 1024
      )
        throw uploadError(
          "上传尝试次数或传输额度已达上限，失败尝试也计入额度",
          429,
        );
      db.prepare("INSERT INTO upload_attempts VALUES(?,?,?,?,?)").run(
        randomUUID(),
        user.id,
        user.school_id,
        bytes,
        time,
      );
      if (ownTransaction) db.exec("COMMIT");
    } catch (e) {
      if (ownTransaction) db.exec("ROLLBACK");
      throw e;
    }
  }
  function legacy(req, res, next) {
    let release, session;
    try {
      release = acquire(req.user, req.ip);
      const declaredLength = req.get("content-length");
      const length =
        declaredLength === undefined ? maxBytes : Number(declaredLength);
      // Disk/memory destinations still enforce their own per-file limits.
      if (!Number.isSafeInteger(length) || length <= 0 || length > maxBytes)
        throw uploadError("上传大小无效或超过总大小上限", 413);
      session = reserve(req.user, { ttl: 90000, bytes: length });
      chargeTransfer(req.user, length);
      let received = 0,
        done = false;
      const timer = setTimeout(() => req.destroy(), 60000);
      const onData = (chunk) => {
        received += chunk.length;
        if (received > maxBytes) req.destroy();
      };
      req.on("data", onData);
      const finish = () => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        req.off("data", onData);
        release();
        db.prepare(
          "UPDATE upload_sessions SET status=?,actual_bytes=? WHERE id=?",
        ).run(
          res.statusCode < 400 ? "complete" : "cancelled",
          received,
          session.id,
        );
      };
      res.once("finish", finish);
      res.once("close", finish);
      next();
    } catch (e) {
      release?.();
      if (session)
        db.prepare(
          "UPDATE upload_sessions SET status='cancelled' WHERE id=?",
        ).run(session.id);
      res
        .set("Connection", "close")
        .status(e.status || 400)
        .json({ error: e.message });
    }
  }
  return { reserve, acquire, chargeTransfer, legacy, maxBytes };
}
