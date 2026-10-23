import { expect, it } from "vitest";
import express from "express";
import http from "node:http";
import { once } from "node:events";
import { limitJsonBodies } from "./body-admission.mjs";
it("bounds simultaneous JSON buffers, keeps GET available and recovers after disconnect", async () => {
  const app = express();
  app.use(limitJsonBodies({ concurrency: 2 }));
  app.use(express.json({ limit: "2mb" }));
  app.get("/health", (_req, res) => res.json({ ok: true }));
  app.post("/test", (req, res) => res.json(req.body));
  app.use((_error, _req, res, _next) => {
    if (!res.destroyed) res.status(400).end();
  });
  const server = app.listen(0, "127.0.0.1");
  await once(server, "listening");
  const base = `http://127.0.0.1:${(server.address() as any).port}`;
  const held = Array.from({ length: 2 }, () => {
    const req = http.request(base + "/test", {
      method: "POST",
      headers: { "content-type": "application/json", "content-length": "1000" },
    });
    req.on("error", () => {});
    req.write("{");
    return req;
  });
  try {
    await new Promise((r) => setTimeout(r, 50));
    const rejected = await fetch(base + "/test", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: "{}",
    });
    expect(rejected.status).toBe(503);
    expect((await fetch(base + "/health")).status).toBe(200);
    held.forEach((r) => r.destroy());
    await new Promise((r) => setTimeout(r, 50));
    const recovered = await fetch(base + "/test", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: '{"recovered":true}',
    });
    expect(await recovered.json()).toEqual({ recovered: true });
  } finally {
    held.forEach((r) => r.destroy());
    server.closeAllConnections();
    await new Promise<void>((r) => server.close(() => r()));
  }
});
