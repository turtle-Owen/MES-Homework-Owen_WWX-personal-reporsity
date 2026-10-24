import { it, expect } from "vitest";
import { createCanvas } from "@napi-rs/canvas";
import { imageHeader } from "./image-header.mjs";
import { tilePlan } from "../shared/tile-plan.mjs";
it("reads JPEG/PNG/WebP dimensions without allocating decoded pixels", () => {
  const canvas = createCanvas(1300, 900);
  for (const mime of ["image/jpeg", "image/png", "image/webp"] as const) {
    const bytes = canvas.toBuffer(mime as any);
    expect(imageHeader(bytes.subarray(0, 65536))).toMatchObject({
      mime,
      width: 1300,
      height: 900,
    });
  }
  expect(() => imageHeader(Buffer.from("<svg width='1300'/>"))).toThrow();
  const png = canvas.toBuffer("image/png");
  png.writeUInt32BE(1000000, 16);
  expect(() => imageHeader(png)).toThrow(/尺寸超限/);
});
it("produces bounded overlapping tiles including small edges and rejects oversized pages", () => {
  const plan = tilePlan(2400, 3200);
  expect(plan[0]).toEqual({ x: 0, y: 0, width: 1300, height: 1300 });
  expect(plan.at(-1)).toMatchObject({ x: 1100, y: 1900 });
  expect(
    plan.every(
      (t) =>
        t.x >= 0 && t.y >= 0 && t.x + t.width <= 2400 && t.y + t.height <= 3200,
    ),
  ).toBe(true);
  expect(tilePlan(200, 300)).toEqual([{ x: 0, y: 0, width: 200, height: 300 }]);
  expect(() => tilePlan(8000, 8000)).toThrow();
});
