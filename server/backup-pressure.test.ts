import { expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createBackupGate } from "../tools/ops/backup-pressure.mjs";
it("defers on fresh API pressure and local memory pressure, but ignores old hints from a stopped API", async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "backup-pressure-"));
  let time = 1000,
    available = 2 * 1024 ** 3;
  const gate = createBackupGate(dir, {
    now: () => time,
    probe: async () => ({
      supported: true,
      totalBytes: 4 * 1024 ** 3,
      availableBytes: available,
      memorySome: 0,
      memoryFull: 0,
      ioFull: 0,
      swapPagesPerSecond: 0,
    }),
  });
  try {
    await gate.ready;
    gate.check();
    fs.writeFileSync(
      path.join(dir, ".resource-pressure.json"),
      JSON.stringify({ version: 1, level: "limited", updatedAt: time }),
    );
    time += 5001;
    expect(() => gate.check()).toThrow(/延后/);
    time += 16000;
    gate.check();
    available = 100 * 1024 ** 2;
    await gate.refresh();
    expect(() => gate.check()).toThrow(/延后/);
  } finally {
    gate.stop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
