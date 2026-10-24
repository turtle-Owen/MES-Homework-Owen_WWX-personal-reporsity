import { api, json } from "./api";
type Part = {
  index: number;
  kind: "original" | "tile";
  page: number;
  width: number;
  height: number;
  x?: number;
  y?: number;
  maxBytes: number;
};
type Session = {
  id: string;
  status: string;
  parts: Part[];
  received: { index_no: number; sha256: string }[];
  submissionId?: string;
};
export async function uploadTiledSubmission(
  files: File[],
  assignment: string,
  studentId: string,
  {
    signal,
    progress,
    sessionId,
    remember,
  }: {
    signal: AbortSignal;
    progress: (text: string) => void;
    sessionId: string | null;
    remember: (id: string) => void;
  },
) {
  if (!globalThis.crypto?.subtle)
    throw new Error("设备端切片需要 HTTPS 或本机安全连接，请使用普通上传");
  if (
    !files.length ||
    files.length > 10 ||
    files.some(
      (f) => !["image/jpeg", "image/png", "image/webp"].includes(f.type),
    )
  )
    throw new Error(
      "设备端切片仅支持 1–10 张 JPG、PNG、WebP 照片；PDF/Word 请使用普通上传",
    );
  const worker = new Worker(new URL("./tile-worker.ts", import.meta.url), {
    type: "module",
  });
  let next = 0;
  const pending = new Map<
    number,
    { resolve: (v: any) => void; reject: (e: Error) => void }
  >();
  const abort = () => {
    worker.terminate();
    for (const p of pending.values())
      p.reject(new Error("上传已暂停，可在当前页面重试"));
    pending.clear();
  };
  signal.addEventListener("abort", abort, { once: true });
  worker.onmessage = ({ data }) => {
    const p = pending.get(data.id);
    if (!p) return;
    pending.delete(data.id);
    if (data.error) p.reject(new Error(data.error));
    else p.resolve(data.result);
  };
  worker.onerror = () => {
    for (const p of pending.values())
      p.reject(new Error("设备处理图片失败，请使用普通上传"));
    pending.clear();
  };
  const call = (type: string, data: object): Promise<any> => {
    signal.throwIfAborted();
    return new Promise((resolve, reject) => {
      const id = ++next;
      pending.set(id, { resolve, reject });
      worker.postMessage({ id, type, ...data });
    });
  };
  try {
    progress("正在设备上检查照片…");
    const pages = await call("inspect", { files });
    const session = sessionId
      ? await api<Session>(`/upload-sessions/${sessionId}`, { signal })
      : await api<Session>(`/assignments/${assignment}/upload-sessions`, {
          ...json("POST", { pages, studentId }),
          signal,
        });
    remember(session.id);
    if (session.status === "complete") return { id: session.submissionId };
    const originals = session.parts.filter((p) => p.kind === "original");
    if (
      originals.length !== pages.length ||
      originals.some(
        (p, i) => p.width !== pages[i].width || p.height !== pages[i].height,
      )
    )
      throw new Error("照片与已有上传会话不一致，请取消后重新上传");
    const received = new Map(
      session.received.map((p) => [p.index_no, p.sha256]),
    );
    let currentPage = -1;
    for (const [position, part] of session.parts.entries()) {
      signal.throwIfAborted();
      let body: Blob;
      if (part.kind === "original") body = files[part.page];
      else {
        progress(`正在切分第 ${part.page + 1} 页…`);
        if (currentPage !== part.page) {
          await call("open", { file: files[part.page] });
          currentPage = part.page;
        }
        body = await call("tile", { part });
      }
      if (body.size > part.maxBytes)
        throw new Error("切片文件过大，请换用普通上传或分次拍摄");
      const buffer = await body.arrayBuffer();
      const hash = [
        ...new Uint8Array(await crypto.subtle.digest("SHA-256", buffer)),
      ]
        .map((x) => x.toString(16).padStart(2, "0"))
        .join("");
      if (received.has(part.index)) {
        if (received.get(part.index) !== hash)
          throw new Error("照片内容与已上传部分不一致，请取消后重新上传");
        continue;
      }
      progress(`正在上传 ${position + 1}/${session.parts.length}…`);
      // A retry uses the same session/index/hash. The server acknowledges an already stored part.
      let error: Error | undefined;
      for (let attempt = 0; attempt < 2; attempt++) {
        try {
          await api(`/upload-sessions/${session.id}/parts/${part.index}`, {
            method: "PUT",
            body,
            signal,
            headers: { "content-type": body.type, "x-content-sha256": hash },
          });
          error = undefined;
          break;
        } catch (e) {
          error = e as Error;
          if (signal.aborted) throw e;
        }
      }
      if (error) throw error;
    }
    progress("正在确认全部文件…");
    return await api<{ id: string }>(
      `/upload-sessions/${session.id}/finalize`,
      { method: "POST", signal },
    );
  } finally {
    signal.removeEventListener("abort", abort);
    worker.terminate();
  }
}
