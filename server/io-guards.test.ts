import { expect, it } from "vitest";
import { requestJson } from "./io-guards.mjs";

it("cancels an oversized chunked response without buffering it all", async () => {
  let cancelled = false,
    reads = 0;
  const response = new Response(
    new ReadableStream({
      pull(controller) {
        reads++;
        controller.enqueue(new Uint8Array(512));
      },
      cancel() {
        cancelled = true;
      },
    }),
  );
  await expect(
    requestJson(
      "https://mock.invalid",
      {},
      {
        request: async () => response,
        maxBytes: 1024,
      },
    ),
  ).rejects.toMatchObject({ code: "RESPONSE_TOO_LARGE" });
  expect(cancelled).toBe(true);
  expect(reads).toBeLessThanOrEqual(5);
});

it("times out a stalled response body and closes the underlying reader", async () => {
  let cancelled = false,
    signal: AbortSignal;
  const response = new Response(
    new ReadableStream({
      start(controller) {
        controller.enqueue(new TextEncoder().encode('{"partial":'));
      },
      cancel() {
        cancelled = true;
      },
    }),
  );
  await expect(
    requestJson(
      "https://mock.invalid",
      {},
      {
        request: async (_url, options) => {
          signal = options.signal;
          return response;
        },
        idleMs: 20,
        timeoutMs: 200,
      },
    ),
  ).rejects.toMatchObject({ code: "IO_TIMEOUT" });
  expect(cancelled).toBe(true);
  expect(signal!.aborted).toBe(true);
});

it("aborts while waiting for headers and remains usable on the next request", async () => {
  await expect(
    requestJson(
      "https://mock.invalid",
      {},
      {
        request: () => new Promise(() => {}),
        timeoutMs: 20,
      },
    ),
  ).rejects.toMatchObject({ code: "IO_TIMEOUT" });
  const { body } = await requestJson(
    "https://mock.invalid",
    {},
    {
      request: async () => Response.json({ recovered: true }),
    },
  );
  expect(body).toEqual({ recovered: true });
});

it("cancels error bodies without consuming unbounded provider diagnostics", async () => {
  let cancelled = false;
  const { response, body } = await requestJson(
    "https://mock.invalid",
    {},
    {
      request: async () =>
        new Response(
          new ReadableStream({
            cancel() {
              cancelled = true;
            },
          }),
          { status: 503 },
        ),
    },
  );
  expect(response.status).toBe(503);
  expect(body).toBeNull();
  expect(cancelled).toBe(true);
});

it("propagates caller cancellation even when the provider never responds", async () => {
  const controller = new AbortController();
  const call = requestJson(
    "https://mock.invalid",
    { signal: controller.signal },
    {
      request: () => new Promise(() => {}),
    },
  );
  controller.abort(new Error("client disconnected"));
  await expect(call).rejects.toThrow("client disconnected");
});
