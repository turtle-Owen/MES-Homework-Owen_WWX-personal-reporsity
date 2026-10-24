import { it, expect, vi, afterEach } from "vitest";
import { DatabaseSync } from "node:sqlite";
import fs from "node:fs";
import os from "node:os";
import { createUploadAdmission } from "./upload-admission.mjs";
afterEach(() => vi.unstubAllEnvs());
it("bounds ingress before reading bytes and charges even failed transfer attempts", () => {
  vi.stubEnv("UPLOAD_CONCURRENCY", "1");
  vi.stubEnv("UPLOAD_USER_DAILY_MB", "1");
  const db = new DatabaseSync(":memory:");
  db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
  const admission = createUploadAdmission(db, os.tmpdir());
  const user = { id: "u", school_id: "school" };
  try {
    const release = admission.acquire(user, "127.0.0.1");
    expect(() =>
      admission.acquire({ id: "other", school_id: "other" }, "127.0.0.2"),
    ).toThrow(/正在上传/);
    release();
    admission.chargeTransfer(user, 800000); // reservation is retained even if a later hash/header check fails.
    expect(() => admission.chargeTransfer(user, 800000)).toThrow(/传输额度/);
    expect(db.prepare("SELECT COUNT(*) n FROM upload_attempts").get()!.n).toBe(
      1,
    );
    db.exec("UPDATE upload_writers SET expires_at=0");
    admission.acquire(user, "127.0.0.1")();
  } finally {
    db.close();
  }
});
it("bounds scratch reservations, authenticates writers separately and recovers expired admission", () => {
  vi.stubEnv("UPLOAD_SCRATCH_MB", "50");
  const db = new DatabaseSync(":memory:");
  db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
  const admission = createUploadAdmission(db, os.tmpdir());
  const user = { id: "u", school_id: "school" };
  try {
    const session = admission.reserve(user);
    expect(() => admission.reserve({ id: "v", school_id: "other" })).toThrow(
      /暂存空间/,
    );
    db.prepare("UPDATE upload_sessions SET expires_at=0 WHERE id=?").run(
      session.id,
    );
    expect(admission.reserve(user).id).not.toBe(session.id);
  } finally {
    db.close();
  }
});
