import { expect, it } from "vitest";
import { scryptSync } from "node:crypto";
import { createPasswordHasher } from "./passwords.mjs";
const tick = () => new Promise((r) => setTimeout(r, 0));
it("keeps the existing password format and returns work to native workers", async () => {
  const passwords = createPasswordHasher();
  const expected = scryptSync("ChangeMe123!", "salt", 64).toString("hex");
  expect(await passwords.hash("ChangeMe123!", "salt")).toBe(expected);
});
it("bounds queued/native hashes and prioritizes interactive work over a roster", async () => {
  const calls: string[] = [],
    finish: Array<(value: string) => void> = [];
  const passwords = createPasswordHasher({
    concurrency: 1,
    maxPending: 2,
    derive: async (password) => {
      calls.push(password);
      return new Promise((r) => finish.push(r));
    },
  });
  const first = passwords.hash("first", "salt");
  const batch = passwords.hash("batch", "salt", { batch: true });
  const interactive = passwords.hash("interactive", "salt");
  await expect(passwords.hash("overflow", "salt")).rejects.toMatchObject({
    status: 503,
  });
  await tick();
  expect(calls).toEqual(["first"]);
  finish.shift()!("hash");
  await first;
  await tick();
  expect(calls).toEqual(["first", "interactive"]);
  finish.shift()!("hash");
  await interactive;
  await tick();
  expect(calls).toEqual(["first", "interactive", "batch"]);
  finish.shift()!("hash");
  await batch;
});
it("does not release a cancelled native slot before the hash actually returns", async () => {
  let finish: (value: string) => void;
  const passwords = createPasswordHasher({
    concurrency: 1,
    derive: () =>
      new Promise((r) => {
        finish = r;
      }),
  });
  const controller = new AbortController();
  const running = passwords.hash("running", "salt", {
    signal: controller.signal,
  });
  await tick();
  controller.abort(new Error("disconnected"));
  await expect(running).rejects.toThrow("disconnected");
  expect(passwords.snapshot().active).toBe(1);
  finish!("late hash");
  await tick();
  expect(passwords.snapshot().active).toBe(0);
});
it("removes abandoned queued work without hashing it", async () => {
  let finish: (value: string) => void,
    calls = 0;
  const passwords = createPasswordHasher({
    concurrency: 1,
    derive: () => {
      calls++;
      return new Promise((r) => {
        finish = r;
      });
    },
  });
  const running = passwords.hash("running", "salt");
  const controller = new AbortController();
  const queued = passwords.hash("queued", "salt", {
    signal: controller.signal,
  });
  controller.abort(new Error("cancelled"));
  await expect(queued).rejects.toThrow("cancelled");
  expect(passwords.snapshot().pending).toBe(0);
  await tick();
  finish!("hash");
  await running;
  await tick();
  expect(calls).toBe(1);
});
