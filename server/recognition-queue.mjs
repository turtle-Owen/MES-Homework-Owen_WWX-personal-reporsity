import { randomUUID } from "node:crypto";

export const positiveLimit = (name, fallback) => {
  const value = Number(process.env[name]);
  return Number.isSafeInteger(value) && value > 0 ? value : fallback;
};
const failure = (message, status) =>
  Object.assign(new Error(message), { status });
export const publicJob = (job) =>
  job && {
    id: job.id,
    submissionId: job.submission_id,
    status: job.status,
    attempts: job.attempts,
    error: job.error,
    createdAt: job.created_at,
    updatedAt: job.updated_at,
    retryAt: job.status === "queued" ? job.available_at : null,
  };

// No external broker required for the single-machine SQLite deployment.
// Claims and result publication are fenced by a token; a dead worker cannot publish.
export function createRecognitionQueue(
  db,
  execute,
  {
    concurrency = positiveLimit("OCR_CONCURRENCY", 2),
    maxPending = positiveLimit("OCR_MAX_PENDING", 100),
    schoolPending = positiveLimit("OCR_SCHOOL_MAX_PENDING", 25),
    maxAttempts = 3,
    timeoutMs = 120000,
    retryMs = 5000,
    now = Date.now,
  } = {},
) {
  let stopped = false;
  const controllers = new Set();
  const active = (id) =>
    db
      .prepare(
        "SELECT * FROM recognition_jobs WHERE submission_id=? AND status IN('queued','running')",
      )
      .get(id);
  function enqueue(submission, user) {
    db.exec("BEGIN IMMEDIATE");
    try {
      if (active(submission.id)) throw failure("识别正在进行中", 409);
      const counts = db
        .prepare(
          "SELECT COUNT(*) total,COALESCE(SUM(school_id=?),0) school FROM recognition_jobs WHERE status IN('queued','running')",
        )
        .get(user.school_id);
      if (counts.total >= maxPending || counts.school >= schoolPending)
        throw failure("识别队列已满，请稍后重试", 429);
      const id = randomUUID(),
        time = now();
      db.prepare(
        `INSERT INTO recognition_jobs
        (id,submission_id,user_id,school_id,available_at,created_at,updated_at)
        VALUES(?,?,?,?,?,?,?)`,
      ).run(id, submission.id, user.id, user.school_id, time, time, time);
      db.prepare(
        "UPDATE submissions SET status='queued',teacher_reviewed=0,score=NULL WHERE id=?",
      ).run(submission.id);
      db.prepare("INSERT INTO review_audit VALUES(?,?,?,?,?,?,?,?)").run(
        randomUUID(),
        submission.id,
        user.school_id,
        user.id,
        "recognition_requested",
        JSON.stringify({
          status: submission.status,
          reviewed: submission.teacher_reviewed,
          score: submission.score,
        }),
        JSON.stringify({ status: "queued", reviewed: 0, score: null }),
        new Date(time).toISOString(),
      );
      db.exec("COMMIT");
      return db.prepare("SELECT * FROM recognition_jobs WHERE id=?").get(id);
    } catch (e) {
      db.exec("ROLLBACK");
      throw e;
    }
  }
  function recover() {
    const time = now();
    db.exec("BEGIN IMMEDIATE");
    try {
      const expired = db
        .prepare(
          "SELECT * FROM recognition_jobs WHERE status='running' AND lease_until<=?",
        )
        .all(time);
      for (const job of expired) {
        const status = job.attempts < maxAttempts ? "queued" : "failed";
        db.prepare(
          "UPDATE recognition_jobs SET status=?,token=NULL,lease_until=NULL,available_at=?,updated_at=?,error=?,error_status=502 WHERE id=?",
        ).run(
          status,
          time,
          time,
          "任务中断，已恢复；重试可能再次计入模型用量",
          job.id,
        );
        db.prepare("UPDATE submissions SET status=? WHERE id=?").run(
          status,
          job.submission_id,
        );
      }
      db.exec("COMMIT");
    } catch (e) {
      db.exec("ROLLBACK");
      throw e;
    }
  }
  function claim() {
    db.exec("BEGIN IMMEDIATE");
    try {
      const time = now();
      const count = db
        .prepare(
          "SELECT COUNT(*) n FROM recognition_jobs WHERE status='running'",
        )
        .get().n;
      const job =
        count < concurrency &&
        db
          .prepare(
            `SELECT j.* FROM recognition_jobs j
        WHERE j.status='queued' AND j.available_at<=?
          AND NOT EXISTS(SELECT 1 FROM recognition_jobs r WHERE r.user_id=j.user_id AND r.status='running')
        ORDER BY j.available_at,j.created_at,j.id LIMIT 1`,
          )
          .get(time);
      if (job) {
        job.token = randomUUID();
        db.prepare(
          "UPDATE recognition_jobs SET status='running',attempts=attempts+1,lease_until=?,token=?,updated_at=? WHERE id=?",
        ).run(time + timeoutMs + 30000, job.token, time, job.id);
        job.attempts++;
      }
      db.exec("COMMIT");
      return job || null;
    } catch (e) {
      db.exec("ROLLBACK");
      throw e;
    }
  }
  const owns = (job) =>
    !!db
      .prepare(
        "SELECT 1 FROM recognition_jobs WHERE id=? AND token=? AND status='running' AND lease_until>?",
      )
      .get(job.id, job.token, now());
  async function run(job) {
    const controller = new AbortController();
    controllers.add(controller);
    const timer = setTimeout(
      () => controller.abort(new Error("识别任务超时，请重试")),
      timeoutMs,
    );
    const assertLease = () => {
      controller.signal.throwIfAborted();
      if (!owns(job)) throw new Error("任务租约已失效");
    };
    try {
      const result = await execute(job, {
        signal: controller.signal,
        assertLease,
      });
      assertLease();
      db.exec("BEGIN IMMEDIATE");
      try {
        assertLease();
        db.prepare(
          "UPDATE submissions SET status='review',teacher_reviewed=0,score=NULL,ocr_text=?,analysis_json=? WHERE id=?",
        ).run(result.ocrText || "", JSON.stringify(result), job.submission_id);
        db.prepare(
          "UPDATE recognition_jobs SET status='succeeded',token=NULL,lease_until=NULL,error=NULL,error_status=NULL,updated_at=? WHERE id=?",
        ).run(now(), job.id);
        db.exec("COMMIT");
      } catch (e) {
        db.exec("ROLLBACK");
        throw e;
      }
    } catch (e) {
      if (!owns(job)) return;
      const busy = e.code === "MODEL_BUSY";
      const retry =
        (busy || e.retryable === true) &&
        (busy || job.attempts < maxAttempts) &&
        now() - job.created_at < 3600000;
      const status = retry ? "queued" : "failed";
      db.exec("BEGIN IMMEDIATE");
      try {
        db.prepare(
          "UPDATE recognition_jobs SET status=?,attempts=attempts-?,token=NULL,lease_until=NULL,available_at=?,error=?,error_status=?,updated_at=? WHERE id=?",
        ).run(
          status,
          busy ? 1 : 0,
          now() + retryMs * Math.max(1, job.attempts),
          busy
            ? "模型繁忙，等待可用额度"
            : "识别未完成，请检查文件和模型设置后重试",
          e.status || 502,
          now(),
          job.id,
        );
        db.prepare("UPDATE submissions SET status=? WHERE id=?").run(
          status,
          job.submission_id,
        );
        db.exec("COMMIT");
      } catch (error) {
        db.exec("ROLLBACK");
        throw error;
      }
    } finally {
      clearTimeout(timer);
      controllers.delete(controller);
    }
  }
  // Awaitable for deterministic tests; production scheduler does not overlap ticks.
  async function tick() {
    if (stopped) return;
    recover();
    const running = [];
    for (let i = 0; i < concurrency; i++) {
      const job = claim();
      if (!job) break;
      running.push(run(job));
    }
    await Promise.all(running);
  }
  let scheduler;
  function start() {
    if (scheduler || stopped) return;
    const next = async () => {
      try {
        await tick();
      } catch {
        console.error("Recognition scheduler failed; retrying on next tick");
      }
      if (!stopped) scheduler = setTimeout(next, 500);
    };
    scheduler = setTimeout(next, 0);
  }
  return {
    enqueue,
    active,
    recover,
    tick,
    start,
    stop() {
      stopped = true;
      clearTimeout(scheduler);
      for (const c of controllers) c.abort();
    },
  };
}
