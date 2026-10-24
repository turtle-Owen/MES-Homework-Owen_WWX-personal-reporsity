import fs from "node:fs";
import path from "node:path";
import {
  createSystemSampler,
  pressureLevel,
} from "../../server/system-pressure.mjs";
export const deferred = () =>
  Object.assign(new Error("资源繁忙，备份延后重试；已有副本保留"), {
    code: "BACKUP_DEFERRED",
  });

// Cooperative boundaries, not SIGSTOP: leave the last good snapshot intact and
// retry later. The API supplies its event-loop and memory pressure as a hint;
// host/cgroup counters still work when the API is stopped.
export function createBackupGate(
  source,
  { probe = createSystemSampler(), now = Date.now } = {},
) {
  let sample = { supported: false },
    pending,
    closed = false,
    lastRead = -Infinity,
    apiLevel = "normal";
  function refresh() {
    if (pending || closed) return pending;
    pending = Promise.resolve()
      .then(probe)
      .then(
        (value) => {
          sample = value;
        },
        () => {},
      )
      .finally(() => {
        pending = null;
      });
    return pending;
  }
  const ready = refresh();
  const timer = setInterval(refresh, 5000);
  timer.unref();
  return {
    ready,
    check() {
      if (pressureLevel(sample).level > 0) throw deferred();
      if (now() - lastRead >= 5000) {
        lastRead = now();
        apiLevel = "normal";
        const file = path.join(source, ".resource-pressure.json");
        try {
          if (fs.statSync(file).size > 8192) throw deferred();
          const data = JSON.parse(fs.readFileSync(file, "utf8"));
          if (
            data.version === 1 &&
            now() >= data.updatedAt &&
            now() - data.updatedAt <= 15000
          )
            apiLevel = data.level;
        } catch (error) {
          if (error.code !== "ENOENT") throw deferred();
        }
      }
      if (["limited", "critical"].includes(apiLevel)) throw deferred();
    },
    stop() {
      closed = true;
      clearInterval(timer);
    },
    refresh,
  };
}
