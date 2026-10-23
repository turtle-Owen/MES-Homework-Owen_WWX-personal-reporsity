import { expect, it } from "vitest";
import { createResourcePressure } from "./resource-pressure.mjs";

it("closes heavy admission on high RSS and opens again without a restart", async () => {
  let memory = 0;
  const pressure = createResourcePressure("unused", {
    rss: () => memory,
    probe: async () => ({ bavail: 1024 ** 3, bsize: 1 }),
  });
  try {
    await pressure.ready;
    pressure.assertUpload(1024);
    memory = Number.MAX_SAFE_INTEGER;
    expect(() => pressure.assertMemory()).toThrow(/内存繁忙/);
    memory = 0;
    pressure.assertUpload(1024);
  } finally {
    pressure.stop();
  }
});

it("never queues disk probes when one is stuck, rejects stale results, and recovers", async () => {
  let time = 100,
    calls = 0,
    stalled = false;
  let finish: (value: any) => void;
  const pressure = createResourcePressure("unused", {
    now: () => time,
    rss: () => 0,
    probe: () => {
      calls++;
      return stalled
        ? new Promise((resolve) => {
            finish = resolve;
          })
        : Promise.resolve({ bavail: 1024 ** 3, bsize: 1 });
    },
  });
  try {
    await pressure.ready;
    pressure.assertUpload();
    stalled = true;
    const pending = pressure.refresh();
    await Promise.resolve();
    for (let i = 0; i < 100; i++) pressure.refresh();
    expect(calls).toBe(2);
    time += 16000;
    expect(() => pressure.assertUpload()).toThrow(/存储暂不可用/);
    finish!({ bavail: 1, bsize: 1 });
    await pending;
    expect(() => pressure.assertUpload()).toThrow(/可用空间不足/);
    stalled = false;
    await pressure.refresh();
    pressure.assertUpload();
  } finally {
    pressure.stop();
  }
});

it("accounts for reservations between disk snapshots instead of repeatedly spending stale free space", async () => {
  const pressure = createResourcePressure("unused", {
    rss: () => 0,
    probe: async () => ({ bavail: 256 * 1024 * 1024, bsize: 1 }),
  });
  try {
    await pressure.ready;
    pressure.assertUpload(100 * 1024 * 1024);
    pressure.chargeDisk(100 * 1024 * 1024);
    expect(() => pressure.assertUpload(100 * 1024 * 1024)).toThrow(
      /可用空间不足/,
    );
    await pressure.refresh();
    pressure.assertUpload(100 * 1024 * 1024);
  } finally {
    pressure.stop();
  }
});
