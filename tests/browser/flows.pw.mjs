import { test, expect } from "@playwright/test";
const png = Buffer.from(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=",
  "base64",
);
const login = async (page, email = "teacher@fixture.test") => {
  await page.goto("/");
  await page.getByLabel("邮箱", { exact: true }).fill(email);
  await page.getByLabel("密码", { exact: true }).fill("ChangeMe123!");
  await page.getByRole("button", { name: "登录", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "退出登录", exact: true }),
  ).toBeVisible();
};
const configure = async (page) => {
  const r = await page.request.put("/api/settings/provider", {
    data: { provider: "openai", apiKey: "browser-mock-only" },
  });
  expect(r.status()).toBe(200);
};
const assignments = async (page) =>
  (await page.request.get("/api/assignments")).json();
const deferred = () => {
  let resolve;
  const promise = new Promise((r) => (resolve = r));
  return { promise, resolve };
};
test.beforeEach(async ({ page }) => {
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.__errors = errors;
});
test.afterEach(async ({ page }) => {
  expect(page.__errors).toEqual([]);
});

test("login and enrollment boundary are visible in the real UI", async ({
  page,
}) => {
  await page.goto("/");
  await expect(
    page.getByText("教师和学生请联系学校领取账户，学段与班级由学校确认。"),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "教师或学生加入学校" }),
  ).toHaveCount(0);
  await login(page);
  await expect(page.locator(".account-chip")).toContainText("王老师");
  expect(
    (
      await page.request.post("/api/auth/register", {
        data: { role: "student", schoolCode: "anything", className: "other" },
      })
    ).status(),
  ).toBe(403);
  await page.screenshot({
    path: "test-results/teacher-login.png",
    fullPage: true,
  });
});

test("two pages form one submission; OCR and manual scores save consistently", async ({
  page,
}) => {
  await login(page);
  await configure(page);
  const a = (await assignments(page)).find((x) => x.title === "浏览器作业 A");
  const student = (await (await page.request.get("/api/students")).json()).find(
    (x) => x.email === "lin@fixture.test",
  );
  await page.getByRole("button", { name: "作业与上传", exact: true }).click();
  await page.getByLabel("对应的正式作业").selectOption(a.id);
  await page.getByLabel("关联学生账户").selectOption(student.id);
  await page.locator(".upload-box input[type=file]").setInputFiles([
    { name: "page-1.png", mimeType: "image/png", buffer: png },
    { name: "page-2.png", mimeType: "image/png", buffer: png },
  ]);
  await expect(page.getByText("同一份答卷：已选择 2 个文件")).toBeVisible();
  const uploaded = page.waitForResponse(
    (r) =>
      r.url().endsWith(`/api/assignments/${a.id}/submissions`) &&
      r.request().method() === "POST",
  );
  await page.getByRole("button", { name: "保存并进入识别队列" }).click();
  const response = await uploaded;
  expect(response.status()).toBe(200);
  const created = await response.json();
  expect(created).toHaveLength(1);
  const id = created[0].id;
  await page.getByRole("button", { name: "识别与复核", exact: true }).click();
  // Select the newly uploaded queue entry via its fresh status, not a fixture sibling.
  await page
    .locator(".select-list button")
    .filter({ hasText: "林一" })
    .filter({ hasText: "等待识别" })
    .first()
    .click();
  await expect(page.locator(".review-detail a")).toHaveCount(2);
  await page.getByRole("button", { name: "开始 OCR", exact: true }).click();
  const rows = page.locator("fieldset.review-item");
  await expect(rows).toHaveCount(8);
  await expect(rows.first()).toContainText("教师标准答案：");
  await rows.first().getByRole("checkbox").check();
  await expect(rows.first().getByRole("spinbutton")).toHaveValue("5");
  await rows.nth(1).getByRole("spinbutton").fill("1");
  await page.screenshot({
    path: "test-results/manual-review.png",
    fullPage: true,
  });
  const reviewed = page.waitForResponse(
    (r) =>
      r.url().endsWith(`/api/submissions/${id}/review`) &&
      r.request().method() === "POST",
  );
  await page.getByRole("button", { name: "确认复核并写入错题本" }).click();
  expect((await reviewed).status()).toBe(200);
  const saved = await (await page.request.get(`/api/submissions/${id}`)).json();
  expect(saved.score).toBe(6);
  expect(saved.teacher_reviewed).toBe(1);
  expect(saved.files).toHaveLength(2);
});

test("late answer-sheet detail cannot replace the selected student", async ({
  page,
}) => {
  await login(page);
  const rows = await (await page.request.get("/api/submissions")).json();
  const a = rows.find((x) => x.student_name === "林一");
  const b = rows.find((x) => x.student_name === "秦二");
  const held = deferred(),
    release = deferred();
  await page.route(`**/api/submissions/${a.id}`, async (route) => {
    const response = await route.fetch();
    held.resolve();
    await release.promise;
    await route.fulfill({ response });
  });
  await page.getByRole("button", { name: "识别与复核", exact: true }).click();
  await page
    .locator(".select-list button")
    .filter({ hasText: "林一" })
    .first()
    .click();
  await held.promise;
  await page
    .locator(".select-list button")
    .filter({ hasText: "秦二" })
    .first()
    .click();
  await expect(page.locator(".review-detail h2")).toHaveText("秦二");
  const delivered = page.waitForResponse((r) =>
    r.url().endsWith(`/api/submissions/${a.id}`),
  );
  release.resolve();
  await (await delivered).finished();
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
  await page.unroute(`**/api/submissions/${a.id}`);
  await expect(page.locator(".review-detail h2")).toHaveText("秦二");
});

test("late teaching statistics cannot send another assignment question numbers", async ({
  page,
}) => {
  await login(page);
  const all = await assignments(page),
    a = all.find((x) => x.title === "浏览器作业 A"),
    b = all.find((x) => x.title === "浏览器作业 B");
  const held = deferred(),
    release = deferred();
  await page.route(`**/api/teaching/assignments/${a.id}`, async (route) => {
    const response = await route.fetch();
    held.resolve();
    await release.promise;
    await route.fulfill({ response });
  });
  await page.getByRole("button", { name: "AI 教学分析", exact: true }).click();
  await held.promise;
  await page.getByLabel("选择作业").selectOption(b.id);
  await expect(page.getByText("B独有题干1", { exact: true })).toBeVisible();
  const delivered = page.waitForResponse((r) =>
    r.url().endsWith(`/api/teaching/assignments/${a.id}`),
  );
  release.resolve();
  await (await delivered).finished();
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
  await page.unroute(`**/api/teaching/assignments/${a.id}`);
  await expect(page.getByLabel("选择作业")).toHaveValue(b.id);
  await expect(page.getByText("B独有题干1", { exact: true })).toBeVisible();
  const submitted = page.waitForRequest((r) =>
    r
      .url()
      .endsWith(`/api/teaching/assignments/${b.id}/mistakes-to-assignment`),
  );
  await page
    .getByRole("button", { name: "一键生成重练作业", exact: true })
    .click();
  const req = await submitted;
  expect(req.postDataJSON().numbers.length).toBeGreaterThan(0);
  await page.screenshot({
    path: "test-results/teaching-selection.png",
    fullPage: true,
  });
});

test("late generated teaching plan cannot appear on another assignment", async ({
  page,
}) => {
  await login(page);
  await configure(page);
  const all = await assignments(page),
    a = all.find((x) => x.title === "浏览器作业 A"),
    b = all.find((x) => x.title === "浏览器作业 B");
  await page.getByRole("button", { name: "AI 教学分析", exact: true }).click();
  await page.getByLabel("选择作业").selectOption(a.id);
  await expect(page.locator(".ta-metrics")).toBeVisible();
  const held = deferred(),
    release = deferred();
  await page.route(
    `**/api/teaching/assignments/${a.id}/analyze`,
    async (route) => {
      const response = await route.fetch();
      expect(response.status()).toBe(200);
      held.resolve();
      await release.promise;
      await route.fulfill({ response });
    },
  );
  await page
    .locator(".ta-head")
    .getByRole("button", { name: "生成 AI 教学方案", exact: true })
    .click();
  await held.promise;
  await page.getByLabel("选择作业").selectOption(b.id);
  await expect(page.getByText("B独有题干1", { exact: true })).toBeVisible();
  const delivered = page.waitForResponse((r) =>
    r.url().endsWith(`/api/teaching/assignments/${a.id}/analyze`),
  );
  release.resolve();
  await (await delivered).finished();
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
  await page.unroute(`**/api/teaching/assignments/${a.id}/analyze`);
  await expect(page.getByText("MOCK_CLASS_PLAN", { exact: true })).toHaveCount(
    0,
  );
});

test("student UI keeps exercise answers private and exposes only own attachments", async ({
  page,
}) => {
  await login(page, "lin@fixture.test");
  const rows = await (await page.request.get("/api/mistakes")).json();
  expect(rows.length).toBeGreaterThan(0);
  for (const m of rows)
    for (const q of m.practiceQuestions)
      expect(Object.keys(q)).toEqual(["prompt"]);
  await page.getByRole("button", { name: "我的错题", exact: true }).click();
  await expect(
    page.getByRole("heading", { name: "我的错题", exact: true }),
  ).toBeVisible();
  const all = await assignments(page);
  const details = await (
    await page.request.get(`/api/assignments/${all[0].id}`)
  ).json();
  expect(
    details.questions.every((q) => !Object.hasOwn(q, "standard_answer")),
  ).toBe(true);
  await expect(
    page.getByRole("button", { name: "识别与复核", exact: true }),
  ).toHaveCount(0);
  await page.screenshot({
    path: "test-results/student-notebook.png",
    fullPage: true,
  });
});
