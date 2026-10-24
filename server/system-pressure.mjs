import fs from "node:fs";
import path from "node:path";

// Small Linux counters only. Never scan processes, disks or pixel buffers.
const readSmall = async (file) => {
  const fd = await fs.promises.open(file, "r");
  try {
    const buffer = Buffer.alloc(32768);
    const { bytesRead } = await fd.read(buffer, 0, buffer.length, 0);
    return buffer.subarray(0, bytesRead).toString();
  } finally {
    await fd.close();
  }
};
export function parseMemory(text) {
  const values = Object.fromEntries(
    [...text.matchAll(/^(\w+):\s+(\d+)\s+kB/gm)].map((m) => [
      m[1],
      Number(m[2]) * 1024,
    ]),
  );
  if (
    !Number.isFinite(values.MemTotal) ||
    !Number.isFinite(values.MemAvailable)
  )
    throw new Error("内存统计不可用");
  return { totalBytes: values.MemTotal, availableBytes: values.MemAvailable };
}
export function parsePSI(text) {
  return Object.fromEntries(
    [...text.matchAll(/^(some|full) avg10=([\d.]+)/gm)].map((m) => [
      m[1],
      Number(m[2]),
    ]),
  );
}
export function createSystemSampler({
  read = readSmall,
  platform = process.platform,
  now = Date.now,
  cgroupMount = "/sys/fs/cgroup",
} = {}) {
  let group,
    discovered = false,
    previous;
  const optional = (file) => read(file).catch(() => null);
  async function discover() {
    discovered = true;
    const membership = await optional("/proc/self/cgroup");
    const relative = membership?.match(/^0::([^\n]*)/m)?.[1];
    if (relative !== undefined) {
      const candidate = path.resolve(cgroupMount, "." + relative);
      if (
        (candidate === cgroupMount ||
          candidate.startsWith(cgroupMount + path.sep)) &&
        (await optional(path.join(candidate, "memory.current"))) !== null
      )
        group = candidate;
      else if (
        (await optional(path.join(cgroupMount, "memory.current"))) !== null
      )
        group = cgroupMount;
    }
  }
  return async () => {
    if (platform !== "linux") return { supported: false };
    if (!discovered) await discover();
    const [mem, vm, memoryPSI, ioPSI] = await Promise.all([
      read("/proc/meminfo"),
      optional("/proc/vmstat"),
      optional(
        group ? path.join(group, "memory.pressure") : "/proc/pressure/memory",
      ),
      optional(group ? path.join(group, "io.pressure") : "/proc/pressure/io"),
    ]);
    const memory = parseMemory(mem);
    if (group) {
      // Include ancestor limits, not just the leaf service. Reclaimable file
      // cache is excluded from working set so copying files is not itself OOM.
      let current = group;
      while (current.startsWith(cgroupMount)) {
        const [max, used, stats] = await Promise.all([
          optional(path.join(current, "memory.max")),
          optional(path.join(current, "memory.current")),
          optional(path.join(current, "memory.stat")),
        ]);
        const limit = Number(max?.trim()),
          usage = used === null ? NaN : Number(used.trim());
        if (Number.isFinite(limit) && limit > 0 && Number.isFinite(usage)) {
          const inactive = Number(
            stats?.match(/^inactive_file (\d+)/m)?.[1] || 0,
          );
          memory.totalBytes = Math.min(memory.totalBytes, limit);
          memory.availableBytes = Math.min(
            memory.availableBytes,
            Math.max(0, limit - Math.max(0, usage - inactive)),
          );
        }
        if (current === cgroupMount) break;
        current = path.dirname(current);
      }
    }
    const time = now();
    const swapped =
      vm === null
        ? null
        : Number(vm.match(/^pswpin (\d+)/m)?.[1] || 0) +
          Number(vm.match(/^pswpout (\d+)/m)?.[1] || 0);
    const swapPagesPerSecond =
      previous && swapped !== null && time > previous.time
        ? Math.max(
            0,
            ((swapped - previous.swapped) * 1000) / (time - previous.time),
          )
        : 0;
    if (swapped !== null) previous = { swapped, time };
    return {
      supported: true,
      ...memory,
      swapPagesPerSecond,
      memorySome: parsePSI(memoryPSI || "").some || 0,
      memoryFull: parsePSI(memoryPSI || "").full || 0,
      ioFull: parsePSI(ioPSI || "").full || 0,
    };
  };
}
export function pressureLevel(sample, lagMs = 0) {
  if (!sample?.supported) return { level: 0, reasons: [] };
  const ratio = sample.availableBytes / sample.totalBytes;
  const reasons = [];
  let level = 0;
  const add = (condition, severity, reason) => {
    if (condition) {
      level = Math.max(level, severity);
      reasons.push(reason);
    }
  };
  add(
    sample.availableBytes < Math.min(512 * 1024 ** 2, sample.totalBytes * 0.2),
    1,
    "memory_available",
  );
  add(
    sample.availableBytes < Math.min(192 * 1024 ** 2, sample.totalBytes * 0.08),
    2,
    "memory_critical",
  );
  add(sample.memorySome >= 10 || sample.memoryFull >= 1, 1, "memory_stall");
  add(sample.memoryFull >= 10, 2, "memory_stall_critical");
  add(sample.ioFull >= 5, 1, "io_stall");
  add(sample.ioFull >= 25, 2, "io_stall_critical");
  add(sample.swapPagesPerSecond >= 256 && ratio < 0.25, 1, "swap_activity");
  add(
    sample.swapPagesPerSecond >= 1024 &&
      (ratio < 0.15 || sample.memorySome >= 5),
    2,
    "swap_thrashing",
  );
  add(lagMs >= 100, 1, "event_loop_delay");
  add(lagMs >= 500, 2, "event_loop_stall");
  return { level, reasons };
}
