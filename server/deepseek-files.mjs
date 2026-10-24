import fs from "node:fs";
import { randomUUID, createHash } from "node:crypto";
import { positive, uploadError } from "./upload-admission.mjs";
import { requestJson } from "./io-guards.mjs";
const ORIGIN = "https://api.deepseek.com";
export function createDeepSeekFiles(
  db,
  { decrypt, request = (...args) => fetch(...args), now = Date.now } = {},
) {
  let cleaning = false;
  const maxFiles = Math.min(positive("DEEPSEEK_FILE_SLOTS", 128), 10000);
  const maxBytes = Math.min(
    positive("DEEPSEEK_FILE_STORAGE_MB", 128) * 1024 * 1024,
    25 * 1024 ** 3,
  );
  function retireExpired() {
    db.prepare(
      "UPDATE provider_files SET status='expired',encrypted_key='',iv='' WHERE status NOT IN('deleted','expired') AND expires_at<=?",
    ).run(now());
  }
  async function clean() {
    if (cleaning) return;
    cleaning = true;
    try {
      retireExpired();
      db.prepare(
        "DELETE FROM provider_files WHERE status IN('deleted','expired') AND expires_at<?",
      ).run(now() - 86400000);
      const records = db
        .prepare(
          "SELECT * FROM provider_files WHERE remote_id IS NOT NULL AND status IN('ready','cleanup') AND cleanup_after<=? ORDER BY cleanup_after LIMIT 10",
        )
        .all(now());
      for (const row of records) {
        try {
          const { response: res, body } = await requestJson(
            `${ORIGIN}/files/${encodeURIComponent(row.remote_id)}`,
            {
              method: "DELETE",
              redirect: "error",
              signal: AbortSignal.timeout(5000),
              headers: {
                authorization: `Bearer ${decrypt(row.encrypted_key, row.iv)}`,
              },
            },
            { request, timeoutMs: 5000, idleMs: 5000, maxBytes: 65536 },
          );
          if (res.status === 404 || (res.ok && body?.deleted === true))
            db.prepare(
              "UPDATE provider_files SET status='deleted',encrypted_key='',iv='' WHERE id=?",
            ).run(row.id);
          else throw new Error("delete pending");
        } catch {
          db.prepare(
            "UPDATE provider_files SET status='cleanup',attempts=attempts+1,cleanup_after=? WHERE id=?",
          ).run(
            now() + Math.min(300000, 30000 * 2 ** Math.min(row.attempts, 3)),
            row.id,
          );
        }
      }
    } finally {
      cleaning = false;
    }
  }
  async function prepare(images, config, signal) {
    if (
      config.provider !== "deepseek" ||
      new URL(config.endpoint).origin !== ORIGIN ||
      config.model !== "deepseek-flash"
    )
      throw uploadError("Files API 仅支持官方 DeepSeek Flash 配置", 409);
    const key = decrypt(config.encrypted_key, config.iv),
      fingerprint = createHash("sha256").update(key).digest("hex");
    const owned = [];
    const release = () => {
      for (const id of owned)
        db.prepare(
          "UPDATE provider_files SET status='cleanup',cleanup_after=? WHERE id=? AND remote_id IS NOT NULL AND status='ready'",
        ).run(now(), id);
      void clean().catch(() => {});
    };
    try {
      const blocks = [];
      for (const image of images) {
        signal?.throwIfAborted();
        let blob;
        if (image.filePath)
          blob = await fs.openAsBlob(image.filePath, { type: image.mimeType });
        else
          blob = new Blob([Buffer.from(image.base64, "base64")], {
            type: image.mimeType,
          });
        if (blob.size > 64 * 1024 * 1024)
          throw uploadError("图片超过 Files API 大小限制", 413);
        const id = randomUUID(),
          time = now();
        db.exec("BEGIN IMMEDIATE");
        try {
          retireExpired();
          const used = db
            .prepare(
              "SELECT COUNT(*) count,COALESCE(SUM(bytes),0) bytes FROM provider_files WHERE status NOT IN('deleted','expired')",
            )
            .get();
          if (used.count >= maxFiles || used.bytes + blob.size > maxBytes)
            throw uploadError("临时模型文件额度已满，请等待清理后重试", 429);
          db.prepare(
            "INSERT INTO provider_files VALUES(?,?,?,?,?,NULL,?,'uploading',?,?,?,0)",
          ).run(
            id,
            config.user_id,
            fingerprint,
            config.encrypted_key,
            config.iv,
            blob.size,
            time,
            time + 3600000 + 90000,
            time + 180000,
          );
          db.exec("COMMIT");
        } catch (e) {
          db.exec("ROLLBACK");
          throw e;
        }
        owned.push(id);
        const form = new FormData();
        form.set("purpose", "user_data");
        form.set("expires_after[anchor]", "created_at");
        form.set("expires_after[seconds]", "3600");
        form.set(
          "file",
          blob,
          `${id}.${image.mimeType === "image/png" ? "png" : image.mimeType === "image/webp" ? "webp" : "jpg"}`,
        );
        const { response, body: uploaded } = await requestJson(
          `${ORIGIN}/files`,
          {
            method: "POST",
            redirect: "error",
            signal: AbortSignal.any([
              AbortSignal.timeout(15000),
              ...(signal ? [signal] : []),
            ]),
            headers: { authorization: `Bearer ${key}` },
            body: form,
          },
          { request, timeoutMs: 15000, maxBytes: 65536 },
        );
        if (!response.ok) throw uploadError("模型临时文件上传失败", 502);
        if (!/^file-api-[\w-]{1,200}$/.test(uploaded?.id || ""))
          throw uploadError("模型临时文件响应无效", 502);
        // Persist even if the caller was cancelled just as the response arrived.
        db.prepare(
          "UPDATE provider_files SET remote_id=?,status='ready' WHERE id=?",
        ).run(uploaded.id, id);
        blocks.push({ type: "file", file_id: uploaded.id });
      }
      return { blocks, release };
    } catch (e) {
      release();
      throw e;
    }
  }
  const timer = setInterval(() => void clean().catch(() => {}), 30000);
  timer.unref();
  return {
    prepare,
    clean,
    stop() {
      clearInterval(timer);
    },
  };
}
