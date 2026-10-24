import fs from "node:fs";
const uploadError = (message, status) =>
  Object.assign(new Error(message), { status });

// A single async probe prevents slow statfs calls from accumulating in libuv.
// Stale or failed probes close admission; they never block the HTTP event loop.
export function createResourcePressure(
  dataDir,
  {
    probe = () => fs.promises.statfs(dataDir),
    rss = () => process.memoryUsage.rss(),
    now = Date.now,
  } = {},
) {
  const configured = Number(process.env.SERVICE_RSS_SOFT_MB);
  const rssLimit =
    (Number.isSafeInteger(configured) && configured > 0 ? configured : 1536) *
    1024 *
    1024;
  let diskDebt = 0;
  let disk,
    checkedAt = 0,
    pending,
    closed = false;
  function refresh() {
    if (pending || closed) return pending;
    const debtAtStart = diskDebt;
    pending = Promise.resolve()
      .then(probe)
      .then(
        (value) => {
          disk = value.bavail * value.bsize;
          diskDebt -= debtAtStart;
          checkedAt = now();
        },
        () => {
          disk = undefined;
        },
      )
      .finally(() => {
        pending = null;
      });
    return pending;
  }
  function assertMemory() {
    if (rss() >= rssLimit) throw uploadError("服务器内存繁忙，请稍后重试", 503);
  }
  function assertUpload(reservedBytes = 0) {
    assertMemory();
    if (disk === undefined || now() - checkedAt > 15000)
      throw uploadError("存储暂不可用，请稍后重试", 503);
    if (disk - diskDebt < reservedBytes + 128 * 1024 * 1024)
      throw uploadError("服务器可用空间不足，暂不能接收文件", 507);
  }
  const ready = refresh();
  const timer = setInterval(refresh, 5000);
  timer.unref();
  return {
    assertMemory,
    assertUpload,
    chargeDisk(bytes) {
      diskDebt += bytes;
    },
    ready,
    refresh,
    stop() {
      closed = true;
      clearInterval(timer);
    },
  };
}
