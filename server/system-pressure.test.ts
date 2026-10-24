import { expect, it } from "vitest";
import {
  createSystemSampler,
  parseMemory,
  pressureLevel,
} from "./system-pressure.mjs";
const MiB = 1024 ** 2;
const healthy = {
  supported: true,
  totalBytes: 4 * 1024 * MiB,
  availableBytes: 2 * 1024 * MiB,
  memorySome: 0,
  memoryFull: 0,
  ioFull: 0,
  swapPagesPerSecond: 0,
};
it("uses MemAvailable and the tightest ancestor cgroup working-set budget", async () => {
  let time = 1000,
    swap = 100;
  const files = new Map([
    ["/proc/self/cgroup", "0::/app\n"],
    [
      "/proc/meminfo",
      "MemTotal: 4194304 kB\nMemFree: 1 kB\nMemAvailable: 3145728 kB\n",
    ],
    ["/sys/fs/cgroup/app/memory.max", String(1024 * MiB)],
    ["/sys/fs/cgroup/app/memory.current", String(300 * MiB)],
    ["/sys/fs/cgroup/app/memory.stat", `inactive_file ${100 * MiB}\n`],
    ["/sys/fs/cgroup/memory.max", String(512 * MiB)],
    ["/sys/fs/cgroup/memory.current", String(400 * MiB)],
    ["/sys/fs/cgroup/memory.stat", `inactive_file ${100 * MiB}\n`],
    [
      "/sys/fs/cgroup/app/memory.pressure",
      "some avg10=0.10 avg60=0.00 total=1\nfull avg10=0.00 avg60=0.00 total=1",
    ],
    ["/sys/fs/cgroup/app/io.pressure", "some avg10=0.00\nfull avg10=0.00"],
  ]);
  const probe = createSystemSampler({
    platform: "linux",
    now: () => time,
    read: async (file) => {
      if (file === "/proc/vmstat") return `pswpin ${swap}\npswpout 0\n`;
      if (!files.has(file)) throw new Error("missing");
      return files.get(file)!;
    },
  });
  const result = await probe();
  expect(result.totalBytes).toBe(512 * MiB);
  expect(result.availableBytes).toBe(212 * MiB);
  expect(result.memorySome).toBe(0.1);
  time += 1000;
  swap += 500;
  expect((await probe()).swapPagesPerSecond).toBe(500);
});
it("does not confuse swap occupancy/activity alone with an imminent memory failure", () => {
  expect(pressureLevel({ ...healthy, swapPagesPerSecond: 10000 }).level).toBe(
    0,
  );
  expect(pressureLevel({ ...healthy, availableBytes: 400 * MiB }).level).toBe(
    1,
  );
  expect(pressureLevel({ ...healthy, availableBytes: 100 * MiB }).level).toBe(
    2,
  );
  expect(pressureLevel({ ...healthy, ioFull: 30 }).level).toBe(2);
  expect(pressureLevel({ ...healthy, memoryFull: 2 }).level).toBe(1);
  expect(pressureLevel(healthy, 600).level).toBe(2);
});
it("supports a small backup cgroup and degrades gracefully on non-Linux platforms", async () => {
  expect(
    pressureLevel({
      ...healthy,
      totalBytes: 256 * MiB,
      availableBytes: 200 * MiB,
    }).level,
  ).toBe(0);
  expect(await createSystemSampler({ platform: "win32" })()).toEqual({
    supported: false,
  });
  expect(() => parseMemory("missing")).toThrow(/不可用/);
});
