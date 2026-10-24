import { expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { recognitionFiles } from "./recognition-files.mjs";
const pause = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
it("isolates a parser crash, releases its slot and processes the next photo", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "parser-isolation-"));
  try {
    const entry = path.join(dir, "crash.mjs");
    fs.writeFileSync(
      entry,
      "process.once('message',()=>process.kill(process.pid,'SIGKILL'));",
    );
    await expect(
      recognitionFiles([], AbortSignal.timeout(5000), { entry }),
    ).rejects.toThrow(/进程退出/);
    // A photo passes by immutable file reference; no pixel decode or Base64 IPC.
    const result = await recognitionFiles(
      [{ file_path: "photo.jpg", file_type: "image/jpeg" }],
      AbortSignal.timeout(5000),
    );
    expect(result.images).toEqual([
      { filePath: "photo.jpg", mimeType: "image/jpeg" },
    ]);
    await pause(100);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
it.runIf(process.platform === "linux")(
  "kills a parser exceeding sampled RSS without terminating the API",
  async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "parser-rss-"));
    try {
      const entry = path.join(dir, "memory.mjs");
      fs.writeFileSync(
        entry,
        "process.once('message',()=>{globalThis.data=new Uint8Array(160*1024*1024).fill(1);setInterval(()=>{},1000);});",
      );
      await expect(
        recognitionFiles([], AbortSignal.timeout(5000), {
          entry,
          rssMB: 100,
          intervalMs: 20,
        }),
      ).rejects.toThrow(/内存超限/);
      await pause(100);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  },
);
it("cancels a stuck parser and bounds admission until it has actually exited", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "parser-abort-"));
  try {
    const entry = path.join(dir, "wait.mjs");
    fs.writeFileSync(
      entry,
      "process.once('message',()=>setInterval(()=>{},1000));",
    );
    const controller = new AbortController();
    const pending = recognitionFiles([], controller.signal, { entry });
    await expect(
      recognitionFiles([], AbortSignal.timeout(5000)),
    ).rejects.toMatchObject({ code: "MODEL_BUSY" });
    controller.abort(new Error("disconnected"));
    await expect(pending).rejects.toThrow("disconnected");
    await pause(100);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
