import { Worker } from "node:worker_threads";

// CPU-heavy parsers and PDF rendering must not block login/API requests.
export function recognitionFiles(files, signal) {
  return new Promise((resolve, reject) => {
    const worker = new Worker(
      new URL("./recognition-files-worker.mjs", import.meta.url),
      {
        workerData: files,
        execArgv: [],
        resourceLimits: { maxOldGenerationSizeMb: 256 },
      },
    );
    let settled = false;
    const finish = (error, value) => {
      if (settled) return;
      settled = true;
      signal?.removeEventListener("abort", abort);
      void worker.terminate();
      if (error) reject(error);
      else resolve(value);
    };
    const abort = () => finish(signal.reason || new Error("文件处理已取消"));
    worker.once("message", (message) =>
      finish(message.error ? new Error(message.error) : null, message.result),
    );
    worker.once("error", (e) => finish(e));
    worker.once("exit", (code) =>
      finish(new Error(`文件处理进程退出 (${code})`)),
    );
    if (signal?.aborted) abort();
    else signal?.addEventListener("abort", abort, { once: true });
  });
}
