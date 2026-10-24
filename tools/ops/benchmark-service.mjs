// Disposable synthetic fixture only: no production data, model calls or credentials.
import { spawn } from "node:child_process";
import { once } from "node:events";
import fs from "node:fs";
import net from "node:net";
import { fileURLToPath } from "node:url";
const root = fileURLToPath(new URL("../..", import.meta.url));
const server = net.createServer().listen(0, "127.0.0.1");
await once(server, "listening");
const port = server.address().port;
await new Promise((r) => server.close(r));
const child = spawn(process.execPath, ["tests/browser/server.mjs"], {
  cwd: root,
  stdio: "ignore",
  env: { ...process.env, FIXTURE_PORT: String(port) },
});
const base = "http://127.0.0.1:" + port;
const pause = (ms) => new Promise((r) => setTimeout(r, ms));
const login = () =>
  fetch(base + "/api/auth/login", {
    signal: AbortSignal.timeout(10000),
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      email: "teacher@fixture.test",
      password: "ChangeMe123!",
    }),
  });
try {
  let ready = false;
  for (let i = 0; i < 150; i++) {
    if (child.exitCode !== null)
      throw new Error("fixture exited before startup");
    try {
      if (
        (
          await fetch(base + "/api/health", {
            signal: AbortSignal.timeout(1000),
          })
        ).ok
      ) {
        ready = true;
        break;
      }
    } catch {}
    await pause(50);
  }
  if (!ready) throw new Error("fixture startup timed out");
  const first = await login();
  if (!first.ok) throw new Error("fixture login failed");
  const cookie = first.headers.get("set-cookie").split(";")[0];
  const read = () =>
    fetch(base + "/api/assignments", {
      headers: { cookie },
      signal: AbortSignal.timeout(10000),
    }).then(async (r) => {
      await r.arrayBuffer();
      if (!r.ok) throw Error("read failed");
    });
  for (let i = 0; i < 20; i++) await read();
  const results = [];
  for (let round = 0; round < 3; round++) {
    const auth = Promise.all(
      Array.from({ length: 8 }, () =>
        login().then(async (r) => {
          await r.arrayBuffer();
          if (r.status !== 200) throw Error("login failed " + r.status);
        }),
      ),
    );
    await pause(5);
    const times = [];
    await Promise.all(
      Array.from({ length: 40 }, async () => {
        const t = performance.now();
        await read();
        times.push(performance.now() - t);
      }),
    );
    await auth;
    times.sort((a, b) => a - b);
    results.push(Math.round(times[Math.ceil(times.length * 0.95) - 1]));
    await pause(100);
  }
  const status =
    process.platform === "linux"
      ? fs.readFileSync("/proc/" + child.pid + "/status", "utf8")
      : null;
  console.log(
    JSON.stringify({
      loginBurst: 8,
      concurrentReads: 40,
      readP95MsByRound: results,
      rssMiB: status
        ? Math.round(Number(status.match(/^VmRSS:\s+(\d+)/m)[1]) / 1024)
        : null,
    }),
  );
} finally {
  if (child.exitCode === null) {
    const done = once(child, "exit");
    const kill = setTimeout(() => child.kill("SIGKILL"), 10000);
    child.kill();
    await done;
    clearTimeout(kill);
  }
}
