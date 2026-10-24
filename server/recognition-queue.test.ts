import { beforeEach, afterEach, describe, it, expect } from "vitest";
import { DatabaseSync } from "node:sqlite";
import fs from "node:fs";
import { createRecognitionQueue, publicJob } from "./recognition-queue.mjs";
import { seedTeachingFixture } from "./analysis.fixture.mjs";
let db: DatabaseSync, f: any, submission: any, user: any;
beforeEach(() => {
  db = new DatabaseSync(":memory:");
  db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
  f = seedTeachingFixture(db);
  submission = db
    .prepare("SELECT * FROM submissions WHERE assignment_id=? LIMIT 1")
    .get(f.assignmentId);
  user = db.prepare("SELECT * FROM users WHERE id=?").get(f.teacherId);
});
afterEach(() => db.close());
const result = { ocrText: "mock", items: [] };
describe("durable recognition", () => {
  it("deduplicates, invalidates approval atomically and publishes only to review", async () => {
    const queue = createRecognitionQueue(db, async () => result);
    const job = queue.enqueue(submission, user);
    expect(() => queue.enqueue(submission, user)).toThrow(/正在进行/);
    expect(
      db
        .prepare(
          "SELECT teacher_reviewed,status,score FROM submissions WHERE id=?",
        )
        .get(submission.id),
    ).toMatchObject({ teacher_reviewed: 0, status: "queued", score: null });
    expect(publicJob(job)).not.toHaveProperty("token");
    await queue.tick();
    expect(
      db
        .prepare(
          "SELECT status,teacher_reviewed,ocr_text FROM submissions WHERE id=?",
        )
        .get(submission.id),
    ).toMatchObject({
      status: "review",
      teacher_reviewed: 0,
      ocr_text: "mock",
    });
    expect(db.prepare("SELECT action FROM review_audit").get()).toMatchObject({
      action: "recognition_requested",
    });
  });
  it("bounds pending capacity without invalidating another student's grade", () => {
    const queue = createRecognitionQueue(db, async () => result, {
      maxPending: 1,
    });
    queue.enqueue(submission, user);
    const other = db
      .prepare("SELECT * FROM submissions WHERE id!=? LIMIT 1")
      .get(submission.id)!;
    expect(() => queue.enqueue(other, user)).toThrow(/已满/);
    expect(
      db.prepare("SELECT * FROM submissions WHERE id=?").get(other.id),
    ).toEqual(other);
  });
  it("recovers an expired crash lease and fences off late output", async () => {
    let time = Date.now();
    let resolve!: (result: any) => void;
    const old = createRecognitionQueue(
      db,
      () =>
        new Promise((r) => {
          resolve = r;
        }),
      { now: () => time },
    );
    const job = old.enqueue(submission, user);
    const pending = old.tick();
    time += 160000;
    const restarted = createRecognitionQueue(
      db,
      async () => ({ ocrText: "recovered" }),
      { now: () => time },
    );
    await restarted.tick();
    resolve({ ocrText: "stale" });
    await pending;
    expect(
      db
        .prepare("SELECT ocr_text FROM submissions WHERE id=?")
        .get(submission.id)!.ocr_text,
    ).toBe("recovered");
    expect(
      db
        .prepare("SELECT attempts,status FROM recognition_jobs WHERE id=?")
        .get(job.id),
    ).toMatchObject({ attempts: 2, status: "succeeded" });
  });
  it("retries transient errors only three times, but never retries permanent failures", async () => {
    let time = Date.now(),
      calls = 0;
    const queue = createRecognitionQueue(
      db,
      async () => {
        calls++;
        throw Object.assign(new Error("secret provider response"), {
          retryable: true,
        });
      },
      { now: () => time, retryMs: 1 },
    );
    const job = queue.enqueue(submission, user);
    for (let i = 0; i < 4; i++) {
      await queue.tick();
      time += 100;
    }
    expect(calls).toBe(3);
    expect(
      db
        .prepare("SELECT status,error FROM recognition_jobs WHERE id=?")
        .get(job.id),
    ).toMatchObject({
      status: "failed",
      error: expect.not.stringContaining("secret"),
    });
    const failed = createRecognitionQueue(db, async () => {
      throw new Error("invalid PDF");
    });
    failed.enqueue(submission, user);
    await failed.tick();
    expect(failed.active(submission.id)).toBeUndefined();
  });
  it("waits for model capacity without spending an attempt", async () => {
    const queue = createRecognitionQueue(db, async () => {
      throw Object.assign(new Error("busy"), { code: "MODEL_BUSY" });
    });
    const job = queue.enqueue(submission, user);
    await queue.tick();
    expect(
      db
        .prepare("SELECT attempts,status FROM recognition_jobs WHERE id=?")
        .get(job.id),
    ).toMatchObject({ attempts: 0, status: "queued" });
  });
  it("times out cooperative workers and does not publish their result", async () => {
    const queue = createRecognitionQueue(
      db,
      async (_job, { signal }) => {
        await new Promise((_resolve, reject) =>
          signal.addEventListener("abort", () => reject(signal.reason), {
            once: true,
          }),
        );
        return result;
      },
      { timeoutMs: 10 },
    );
    queue.enqueue(submission, user);
    await queue.tick();
    expect(
      db
        .prepare("SELECT status FROM submissions WHERE id=?")
        .get(submission.id)!.status,
    ).toBe("failed");
  });
});

it("restores prior reviewed grades when a queued call cannot reserve a budget", async () => {
  const queue = createRecognitionQueue(db, async () => {
    throw Object.assign(new Error("quota"), {
      code: "MODEL_QUOTA",
      status: 429,
    });
  });
  const before = db
    .prepare("SELECT status,teacher_reviewed,score FROM submissions WHERE id=?")
    .get(submission.id);
  queue.enqueue(submission, user);
  await queue.tick();
  expect(
    db
      .prepare(
        "SELECT status,teacher_reviewed,score FROM submissions WHERE id=?",
      )
      .get(submission.id),
  ).toEqual(before);
  expect(
    db
      .prepare(
        "SELECT status,error FROM recognition_jobs WHERE submission_id=?",
      )
      .get(submission.id),
  ).toMatchObject({ status: "failed", error: expect.stringContaining("保留") });
});
