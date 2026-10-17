import { beforeAll, afterAll, describe, expect, it } from "vitest";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { DatabaseSync } from "node:sqlite";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import net from "node:net";
import { randomUUID } from "node:crypto";
import { seedTeachingFixture } from "./analysis.fixture.mjs";

const root = path.resolve(import.meta.dirname, "..");
let dir: string,
  base: string,
  db: DatabaseSync,
  child: ReturnType<typeof spawn>;
let fixture: ReturnType<typeof seedTeachingFixture>;
let student: string,
  peer: string,
  teacher: string,
  outsider: string,
  peerTeacher: string;
let submissionId: string,
  filename: string,
  hiddenAssignment: string,
  foreignKnowledge: string;
async function request(
  route: string,
  cookie = "",
  method = "GET",
  body?: unknown,
  headers: Record<string, string> = {},
) {
  return fetch(base + route, {
    method,
    headers: {
      ...(cookie ? { cookie } : {}),
      ...(body !== undefined ? { "content-type": "application/json" } : {}),
      ...headers,
    },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
}
async function login(email: string) {
  const response = await request("/api/auth/login", "", "POST", {
    email,
    password: "ChangeMe123!",
  });
  expect(response.status).toBe(200);
  return response.headers.get("set-cookie")!.split(";")[0];
}
beforeAll(async () => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "mes-security-"));
  fs.mkdirSync(path.join(dir, "uploads"));
  db = new DatabaseSync(path.join(dir, "smart-campus.sqlite"));
  db.exec(fs.readFileSync(path.join(root, "server/schema.sql"), "utf8"));
  fixture = seedTeachingFixture(db);
  const sample = db
    .prepare("SELECT * FROM users WHERE id=?")
    .get(fixture.teacherId)!;
  const otherSchool = randomUUID();
  db.prepare("INSERT INTO schools VALUES(?,?,?)").run(
    otherSchool,
    "Other",
    new Date().toISOString(),
  );
  for (const [email, school] of [
    ["outside@fixture.test", otherSchool],
    ["peer-teacher@fixture.test", fixture.schoolId],
  ]) {
    db.prepare(
      "INSERT INTO users SELECT ?,?,role,name,?,password_hash,salt,stage,subject,class_name,created_at,0 FROM users WHERE id=?",
    ).run(randomUUID(), school, email, sample.id);
  }
  hiddenAssignment = randomUUID();
  db.prepare(
    "INSERT INTO assignments SELECT ?,school_id,teacher_id,title,stage,subject,'other-class',due_at,status,created_at FROM assignments WHERE id=?",
  ).run(hiddenAssignment, fixture.assignmentId);
  foreignKnowledge = randomUUID();
  db.prepare("INSERT INTO knowledge VALUES(?,?,?,?,?,?,?,?)").run(
    foreignKnowledge,
    otherSchool,
    "初中",
    "物理",
    "private",
    "",
    "outside",
    new Date().toISOString(),
  );
  const submission = db
    .prepare(
      "SELECT id FROM submissions WHERE student_id=? AND teacher_reviewed=1",
    )
    .get(fixture.students[0].id)!;
  submissionId = String(submission.id);
  filename = `${submissionId}.png`;
  fs.writeFileSync(
    path.join(dir, "uploads", filename),
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
  );
  db.prepare(
    "UPDATE submissions SET file_path=?,file_type='image/png',analysis_json=? WHERE id=?",
  ).run(
    path.join(dir, "uploads", filename),
    JSON.stringify({ items: [], secret: "old-reviewed-result" }),
    submissionId,
  );
  const listener = net.createServer();
  listener.listen(0, "127.0.0.1");
  await once(listener, "listening");
  const port = (listener.address() as net.AddressInfo).port;
  await new Promise<void>((resolve) => listener.close(() => resolve()));
  base = `http://127.0.0.1:${port}`;
  const preload = path.join(dir, "mock-model.mjs");
  fs.writeFileSync(
    preload,
    `globalThis.fetch = async () => { await new Promise(r => setTimeout(r, 500)); return Response.json({choices:[{message:{content:JSON.stringify({ocrText:"RACE_UNREVIEWED",items:[]})}}]}); };`,
  );
  child = spawn(process.execPath, ["--import", preload, "server/index.mjs"], {
    cwd: root,
    env: {
      ...process.env,
      DATA_DIR: dir,
      PORT: String(port),
      HOST: "127.0.0.1",
      NODE_ENV: "test",
      APP_SECRET: "security-test-only-32-character-secret",
    },
    stdio: "ignore",
  });
  for (let i = 0; i < 100; i++) {
    try {
      if ((await request("/api/health")).ok) break;
    } catch {}
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  teacher = await login("teacher@fixture.test");
  student = await login("lin@fixture.test");
  peer = await login("qing@fixture.test");
  outsider = await login("outside@fixture.test");
  peerTeacher = await login("peer-teacher@fixture.test");
}, 20000);
afterAll(async () => {
  if (child && child.exitCode === null) {
    const exit = once(child, "exit");
    child.kill();
    await exit;
  }
  db?.close();
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
});

describe("HTTP authorization regressions (isolated SQLite; no model requests)", () => {
  it("blocks anonymous, peer, unrelated teacher, and other-school access to private files and answers", async () => {
    for (const cookie of ["", peer, peerTeacher, outsider]) {
      expect([401, 404]).toContain(
        (await request(`/api/submissions/${submissionId}`, cookie)).status,
      );
      expect([401, 404]).toContain(
        (await request(`/uploads/${filename}`, cookie)).status,
      );
    }
    expect(
      (await request(`/api/submissions/${submissionId}`, student)).status,
    ).toBe(200);
    const download = await request(`/uploads/${filename}`, teacher);
    expect(download.status).toBe(200);
    expect(download.headers.get("content-disposition")).toBe("attachment");
  });
  it("hides standard answers and enforces class/teacher scope on assignment details", async () => {
    const assignment = await (
      await request(`/api/assignments/${fixture.assignmentId}`, student)
    ).json();
    expect(assignment.questions).toHaveLength(8);
    expect(
      assignment.questions.every((q: object) => !("standard_answer" in q)),
    ).toBe(true);
    for (const cookie of [student, peer])
      expect(
        (await request(`/api/assignments/${hiddenAssignment}`, cookie)).status,
      ).toBe(404);
    expect(
      (await request(`/api/assignments/${fixture.assignmentId}`, peerTeacher))
        .status,
    ).toBe(404);
    expect(
      (await request(`/api/assignments/${fixture.assignmentId}`, outsider))
        .status,
    ).toBe(404);
  });
  it("denies teacher self-enrollment and student class switching", async () => {
    expect(
      (
        await request("/api/auth/register", "", "POST", {
          role: "teacher",
          schoolCode: fixture.schoolId,
          name: "Attacker",
          email: "attacker@example.test",
          password: "attacker-password",
          stage: "初中",
          subject: "物理",
          className: "八年级（1）班",
        })
      ).status,
    ).toBe(403);
    expect(
      (await request("/api/me", student, "PATCH", { className: "other-class" }))
        .status,
    ).toBe(403);
    expect(
      (await request("/api/me", student, "PATCH", { name: "新姓名" })).status,
    ).toBe(200);
  });
  it("rejects cross-site state changes including login CSRF", async () => {
    expect(
      (
        await request(
          "/api/me",
          teacher,
          "PATCH",
          { name: "attacker" },
          { origin: "https://evil.example" },
        )
      ).status,
    ).toBe(403);
    expect(
      (
        await request(
          "/api/auth/login",
          "",
          "POST",
          { email: "teacher@fixture.test", password: "ChangeMe123!" },
          { "sec-fetch-site": "cross-site" },
        )
      ).status,
    ).toBe(403);
  });
  it("rejects unsafe model destinations and prototype property provider names", async () => {
    for (const provider of ["openai", "__proto__", "constructor"])
      expect(
        (
          await request("/api/settings/provider", teacher, "PUT", {
            provider,
            endpoint: "http://169.254.169.254/",
            apiKey: "not-a-real-key",
          })
        ).status,
      ).toBe(400);
  });
  it("rejects cross-school knowledge and fabricated review question links without changing records", async () => {
    expect(
      (
        await request("/api/assignments", teacher, "POST", {
          title: "test",
          questions: [
            {
              prompt: "q",
              standardAnswer: "a",
              score: 1,
              knowledgeId: foreignKnowledge,
            },
          ],
        })
      ).status,
    ).toBe(400);
    expect(
      (
        await request(
          `/api/submissions/${submissionId}/review`,
          teacher,
          "POST",
          { items: [{ questionId: randomUUID(), isCorrect: false }] },
        )
      ).status,
    ).toBe(400);
    expect(
      db
        .prepare("SELECT teacher_reviewed FROM submissions WHERE id=?")
        .get(submissionId)!.teacher_reviewed,
    ).toBe(1);
  });
  it("rejects active uploads and removes rejected temporary files", async () => {
    const before = fs.readdirSync(path.join(dir, "uploads"));
    const form = new FormData();
    form.append(
      "files",
      new Blob(["<script>alert(1)</script>"], { type: "image/png" }),
      "malicious.html",
    );
    const response = await fetch(
      `${base}/api/assignments/${fixture.assignmentId}/submissions`,
      { method: "POST", headers: { cookie: student }, body: form },
    );
    expect(response.status).toBe(400);
    expect(fs.readdirSync(path.join(dir, "uploads"))).toEqual(before);
    expect(
      (
        await fetch(`${base}/api/assignments/${hiddenAssignment}/submissions`, {
          method: "POST",
          headers: { cookie: student },
          body: form,
        })
      ).status,
    ).toBe(404);
  });
  it("uses canonical upload extensions and preserves authorized teacher review", async () => {
    const form = new FormData();
    form.append(
      "files",
      new Blob([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])], {
        type: "image/png",
      }),
      "image.html",
    );
    const response = await fetch(
      `${base}/api/assignments/${fixture.assignmentId}/submissions`,
      { method: "POST", headers: { cookie: student }, body: form },
    );
    expect(response.status).toBe(200);
    const [{ id }] = await response.json();
    expect(
      String(
        db.prepare("SELECT file_path FROM submissions WHERE id=?").get(id)!
          .file_path,
      ),
    ).toMatch(/\.png$/);
    const items = [...fixture.questionIdByNumber.values()].map(
      (questionId) => ({ questionId, isCorrect: true }),
    );
    expect(
      (
        await request(`/api/submissions/${id}/review`, teacher, "POST", {
          items,
          score: 60,
        })
      ).status,
    ).toBe(200);
  });
  it("hides unreviewed OCR and model output, including after re-recognition", async () => {
    expect(
      (
        await request(
          `/api/submissions/${submissionId}/recognize`,
          teacher,
          "POST",
        )
      ).status,
    ).toBe(502);
    const row = await (
      await request(`/api/submissions/${submissionId}`, student)
    ).json();
    expect(row.teacher_reviewed).toBe(0);
    expect(row.analysis).toBeNull();
    expect(row.analysis_json).toBeNull();
    expect(row.ocr_text).toBeNull();
  });
  it("serializes recognition and review so late model output cannot inherit approval", async () => {
    expect(
      (
        await request("/api/settings/provider", teacher, "PUT", {
          provider: "openai",
          apiKey: "mock-only",
        })
      ).status,
    ).toBe(200);
    const pending = request(
      `/api/submissions/${submissionId}/recognize`,
      teacher,
      "POST",
    );
    for (let i = 0; i < 50; i++) {
      if (
        db
          .prepare("SELECT status FROM submissions WHERE id=?")
          .get(submissionId)!.status === "recognizing"
      )
        break;
      await new Promise((r) => setTimeout(r, 10));
    }
    const items = [...fixture.questionIdByNumber.values()].map(
      (questionId) => ({ questionId, isCorrect: true }),
    );
    expect(
      (
        await request(
          `/api/submissions/${submissionId}/review`,
          teacher,
          "POST",
          { items, score: 60 },
        )
      ).status,
    ).toBe(409);
    expect(
      (
        await request(
          `/api/submissions/${submissionId}/recognize`,
          teacher,
          "POST",
        )
      ).status,
    ).toBe(409);
    expect((await pending).status).toBe(200);
    const visible = await (
      await request(`/api/submissions/${submissionId}`, student)
    ).json();
    expect(visible.teacher_reviewed).toBe(0);
    expect(visible.analysis).toBeNull();
  });
  it("handles malformed password values and limits repeated login attempts", async () => {
    expect(
      (
        await request("/api/auth/login", "", "POST", {
          email: "teacher@fixture.test",
          password: {},
        })
      ).status,
    ).toBe(400);
    let last = 0;
    for (let i = 0; i < 35; i++)
      last = (
        await request("/api/auth/login", "", "POST", {
          email: "missing@test",
          password: "wrong",
        })
      ).status;
    expect(last).toBe(429);
  });
});
