// Windows integration test against the real built executable. No dependency install,
// application deploy, Cloudflare request, DNS mutation or credentials are involved.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

assert.equal(process.platform, 'win32', 'Run this smoke test on a Windows runner');
assert.ok(process.argv[2], 'Usage: node windows-smoke.mjs <campus-deploy.exe>');
const executable = path.resolve(process.argv[2]);
const workdir = await mkdtemp(path.join(os.tmpdir(), 'campus-deploy-smoke-'));
let child;
let output = '';
let port;

// Raw sockets preserve duplicate and attacker-controlled headers verbatim.
async function request(method, route, { host = `127.0.0.1:${port}`, origin, body = '' } = {}) {
  const lines = [`${method} ${route} HTTP/1.1`, `Host: ${host}`, 'Connection: close'];
  if (origin !== undefined) lines.push(`Origin: ${origin}`);
  if (method === 'POST') lines.push('Content-Type: application/json', `Content-Length: ${Buffer.byteLength(body)}`);
  const response = await new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: '127.0.0.1', port });
    let data = '';
    socket.setTimeout(8000, () => socket.destroy(new Error('HTTP smoke request timed out')));
    socket.on('connect', () => socket.write(`${lines.join('\r\n')}\r\n\r\n${body}`));
    socket.on('data', chunk => { data += chunk.toString('utf8'); });
    socket.on('error', reject);
    socket.on('end', () => resolve(data));
  });
  const status = Number(/^HTTP\/1\.[01] (\d{3})/.exec(response)?.[1]);
  return { status, body: response.split('\r\n\r\n').slice(1).join('\r\n\r\n') };
}

try {
  for (const flag of ['--version', '--help']) {
    const result = spawnSync(executable, [flag], { cwd: workdir, encoding: 'utf8', timeout: 10000 });
    assert.ifError(result.error);
    assert.equal(result.status, 0, `${flag}: ${result.stderr}`);
    assert.match(result.stdout, flag === '--version' ? /campus-deploy 1\.0\.0/ : /--no-browser/);
  }
  // Allocate an available port. If another process wins the small bind race, fail
  // instead of accidentally testing a service other than this process.
  const reservation = net.createServer();
  await new Promise(resolve => reservation.listen(0, '127.0.0.1', resolve));
  port = reservation.address().port;
  await new Promise(resolve => reservation.close(resolve));
  child = spawn(executable, ['--web', '--no-browser', '--web-port', String(port)], {
    cwd: workdir, stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stdout.on('data', data => { output += data; });
  child.stderr.on('data', data => { output += data; });
  const exited = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('exit', (code, signal) => resolve({ code, signal }));
  });
  // Install a rejection handler immediately, including when startup fails early.
  exited.catch(() => {});
  let ready = false;
  for (let attempt = 0; attempt < 100; attempt++) {
    assert.equal(child.exitCode, null, `Console exited during startup: ${output}`);
    try {
      const result = await request('GET', '/');
      if (result.status === 200 && result.body.includes('/api/deploy')) { ready = true; break; }
    } catch (error) {
      if (!['ECONNREFUSED', 'ECONNRESET'].includes(error.code)) throw error;
    }
    await delay(100);
  }
  assert.ok(ready, `Console did not become ready: ${output}`);
  assert.equal(child.exitCode, null);
  const status = await request('GET', '/api/status?from=0');
  assert.equal(status.status, 200);
  const snapshot = JSON.parse(status.body);
  assert.equal(snapshot.busy, false);
  assert.equal(snapshot.phase, 'idle');
  assert.ok(Array.isArray(snapshot.logs));
  const origin = `http://127.0.0.1:${port}`;
  for (const [method, route, headers] of [
    ['GET', '/api/status', { host: `attacker.test:${port}` }],
    ['GET', '/api/status', { origin: 'https://attacker.test' }],
    ['POST', '/api/install', { origin: 'https://attacker.test' }],
    ['POST', '/api/deploy', { origin: 'null', body: '{}' }],
    ['POST', '/api/quit', {}],
  ]) {
    const result = await request(method, route, headers);
    assert.equal(result.status, 403, `${method} ${route} should reject untrusted caller`);
  }
  // Invalid same-origin JSON must fail before a worker is started; the console
  // must remain healthy so that a subsequent request can still stop it.
  for (const body of ['not json', '[]', '['.repeat(1000) + '0' + ']'.repeat(1000)]) {
    assert.equal((await request('POST', '/api/deploy', { origin, body })).status, 400);
    assert.equal(JSON.parse((await request('GET', '/api/status')).body).busy, false);
  }
  assert.equal((await request('POST', '/api/stop', { origin })).status, 200);
  assert.equal((await request('GET', '/not-a-route')).status, 404);
  assert.equal((await request('POST', '/api/quit', { origin })).status, 200);
  const result = await Promise.race([exited, delay(10000).then(() => { throw new Error('Console did not exit after quit'); })]);
  assert.equal(result.code, 0, output);
  console.log('Windows real executable smoke passed: CLI, HTTP, Host/Origin boundary, invalid JSON, graceful exit');
} catch (error) {
  console.error(output);
  throw error;
} finally {
  if (child?.pid && child.exitCode === null && child.signalCode === null) {
    const stopped = new Promise(resolve => child.once('exit', resolve));
    child.kill();
    await stopped;
  }
  await rm(workdir, { recursive: true, force: true });
}
