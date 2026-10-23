import { it, expect, vi, afterEach } from "vitest";
import { DatabaseSync } from "node:sqlite";
import fs from "node:fs";
import { createDeepSeekFiles } from "./deepseek-files.mjs";
afterEach(() => vi.unstubAllEnvs());
const config = {
  provider: "deepseek",
  endpoint: "https://api.deepseek.com/chat/completions",
  model: "deepseek-flash",
  user_id: "teacher",
  encrypted_key: "encrypted",
  iv: "iv",
};
it("sets explicit TTL, references file IDs, and retains failed deletion for retry", async () => {
  const db = new DatabaseSync(":memory:");
  db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
  let time = Date.now(),
    deletes = 0,
    uploads = 0;
  const files = createDeepSeekFiles(db, {
    decrypt: () => "mock-only",
    now: () => time,
    request: async (url, options) => {
      expect(url).toMatch(/^https:\/\/api.deepseek.com\/files/);
      if (options.method === "POST") {
        uploads++;
        expect(options.body.get("expires_after[seconds]")).toBe("3600");
        expect(options.body.get("expires_after[anchor]")).toBe("created_at");
        expect(options.body.get("purpose")).toBe("user_data");
        return Response.json({ id: "file-api-mock" });
      }
      deletes++;
      return deletes === 1
        ? new Response("unavailable", { status: 503 })
        : Response.json({ deleted: true });
    },
  });
  try {
    const remote = await files.prepare(
      [{ mimeType: "image/jpeg", base64: "YQ==" }],
      config,
    );
    expect(remote.blocks).toEqual([{ type: "file", file_id: "file-api-mock" }]);
    remote.release();
    await new Promise((r) => setTimeout(r, 10));
    expect(db.prepare("SELECT status FROM provider_files").get()!.status).toBe(
      "cleanup",
    );
    time += 60000;
    await files.clean();
    expect(db.prepare("SELECT status FROM provider_files").get()!.status).toBe(
      "deleted",
    );
    expect(uploads).toBe(1);
  } finally {
    files.stop();
    db.close();
  }
});
it("reserves file slots before upload and holds ambiguous uploads until expiration", async () => {
  vi.stubEnv("DEEPSEEK_FILE_SLOTS", "1");
  const db = new DatabaseSync(":memory:");
  db.exec(fs.readFileSync(new URL("./schema.sql", import.meta.url), "utf8"));
  let time = Date.now(),
    calls = 0;
  const files = createDeepSeekFiles(db, {
    decrypt: () => "mock-only",
    now: () => time,
    request: async () => {
      calls++;
      throw new Error("connection lost");
    },
  });
  try {
    await expect(
      files.prepare([{ mimeType: "image/jpeg", base64: "YQ==" }], config),
    ).rejects.toThrow();
    await expect(
      files.prepare([{ mimeType: "image/jpeg", base64: "YQ==" }], config),
    ).rejects.toThrow(/额度已满/);
    expect(calls).toBe(1);
    time += 3700000;
    await files.clean();
    expect(db.prepare("SELECT status FROM provider_files").get()!.status).toBe(
      "expired",
    );
    await expect(
      files.prepare([], { ...config, endpoint: "https://evil.example/chat" }),
    ).rejects.toThrow(/仅支持官方/);
  } finally {
    files.stop();
    db.close();
  }
});
