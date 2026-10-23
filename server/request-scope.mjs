import { AsyncLocalStorage } from "node:async_hooks";
const requests = new AsyncLocalStorage();
const shutdown = new AbortController();

export function requestSignal(signal = AbortSignal.timeout(120000)) {
  return AbortSignal.any([
    signal,
    shutdown.signal,
    ...(requests.getStore() ? [requests.getStore()] : []),
  ]);
}

export function requestScope(req, res, next) {
  const controller = new AbortController();
  const timer = setTimeout(() => {
    controller.abort(new Error("请求超时"));
    res.destroy();
  }, 240000);
  const close = () => {
    clearTimeout(timer);
    if (!res.writableFinished) controller.abort(new Error("客户端已断开"));
  };
  res.once("close", close);
  res.once("finish", () => clearTimeout(timer));
  requests.run(controller.signal, next);
}

export function cancelRequests() {
  shutdown.abort(new Error("服务正在重启"));
}
