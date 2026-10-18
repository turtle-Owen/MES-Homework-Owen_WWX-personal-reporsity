import { randomUUID } from "node:crypto";

const positiveLimit = (name, fallback) => {
  const value = Number(process.env[name]);
  return Number.isSafeInteger(value) && value > 0 ? value : fallback;
};

export function modelErrorStatus(error) {
  return [409, 429].includes(error?.status) ? error.status : 502;
}

// Reserve before contacting a provider. SQLite makes quotas and active leases
// shared by processes and persistent across restarts; failed calls count too.
export function createModelBudget(db) {
  const limits = {
    student: positiveLimit("MODEL_STUDENT_HOURLY_LIMIT", 20),
    staff: positiveLimit("MODEL_STAFF_HOURLY_LIMIT", 120),
    schoolHour: positiveLimit("MODEL_SCHOOL_HOURLY_LIMIT", 600),
    schoolDay: positiveLimit("MODEL_SCHOOL_DAILY_LIMIT", 3000),
    concurrency: positiveLimit("MODEL_SCHOOL_CONCURRENCY", 4),
  };
  return (userId) => {
    const now = Date.now();
    const user = db
      .prepare("SELECT school_id,role FROM users WHERE id=?")
      .get(userId);
    if (!user) throw new Error("账户不存在");
    db.exec("BEGIN IMMEDIATE");
    let id;
    try {
      db.prepare("DELETE FROM model_usage WHERE created_at<=?").run(
        now - 86400000,
      );
      const usage = db
        .prepare(
          `
        SELECT
          SUM(CASE WHEN user_id=? AND finished_at IS NULL AND lease_until>? THEN 1 ELSE 0 END) user_active,
          SUM(CASE WHEN finished_at IS NULL AND lease_until>? THEN 1 ELSE 0 END) school_active,
          SUM(CASE WHEN user_id=? AND created_at>? THEN 1 ELSE 0 END) user_hour,
          SUM(CASE WHEN created_at>? THEN 1 ELSE 0 END) school_hour,
          COUNT(*) school_day
        FROM model_usage WHERE school_id=?
      `,
        )
        .get(
          userId,
          now,
          now,
          userId,
          now - 3600000,
          now - 3600000,
          user.school_id,
        );
      if (usage.user_active || usage.school_active >= limits.concurrency)
        throw Object.assign(new Error("模型任务正在进行，请等待完成后重试"), {
          status: 429,
        });
      if (
        usage.user_hour >=
          (user.role === "student" ? limits.student : limits.staff) ||
        usage.school_hour >= limits.schoolHour ||
        usage.school_day >= limits.schoolDay
      )
        throw Object.assign(
          new Error("模型调用已达账户或学校限额，请稍后重试"),
          { status: 429 },
        );
      id = randomUUID();
      db.prepare("INSERT INTO model_usage VALUES(?,?,?,?,NULL,?)").run(
        id,
        userId,
        user.school_id,
        now,
        now + 95000,
      );
      db.exec("COMMIT");
    } catch (error) {
      db.exec("ROLLBACK");
      throw error;
    }
    let released = false;
    const release = () => {
      if (released) return;
      db.prepare("UPDATE model_usage SET finished_at=? WHERE id=?").run(
        Date.now(),
        id,
      );
      released = true;
    };
    release.renew = () => {
      const now = Date.now();
      if (
        released ||
        !db
          .prepare(
            "UPDATE model_usage SET lease_until=? WHERE id=? AND finished_at IS NULL AND lease_until>?",
          )
          .run(now + 95000, id, now).changes
      )
        throw Object.assign(new Error("模型任务预留已过期，请重试"), {
          status: 429,
        });
    };
    return release;
  };
}
