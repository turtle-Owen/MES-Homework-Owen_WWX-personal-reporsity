import { describe, expect, it } from "vitest";
import { createTeachingScope } from "./teaching-state";

describe("teaching request isolation", () => {
  it("rejects every old assignment response after switching A to B", async () => {
    const scope = createTeachingScope();
    scope.select("A");
    const requests = [
      "load",
      "analysis",
      "refine",
      "focus:student",
      "reassign",
    ].map((channel) => scope.start(channel));
    scope.select("B");
    const b = scope.start("load");
    let display = "B";
    await Promise.all(
      requests.map(async (ticket) => {
        await Promise.resolve();
        if (scope.isCurrent(ticket)) display = "A";
      }),
    );
    expect(display).toBe("B");
    expect(scope.isCurrent(b)).toBe(true);
  });
  it("does not resurrect A requests after A to B to A", () => {
    const scope = createTeachingScope();
    scope.select("A");
    const old = scope.start("analysis");
    scope.select("B");
    scope.select("A");
    expect(scope.isCurrent(old)).toBe(false);
  });
  it("keeps the latest focus update per student without losing other students", () => {
    const scope = createTeachingScope();
    scope.select("A");
    const old = scope.start("focus:one");
    const other = scope.start("focus:two");
    const current = scope.start("focus:one");
    expect(scope.isCurrent(old)).toBe(false);
    expect(scope.isCurrent(other)).toBe(true);
    expect(scope.isCurrent(current)).toBe(true);
  });
  it("invalidates pending detail and generation when closing the student panel", () => {
    const scope = createTeachingScope();
    scope.select("assignment:student");
    const detail = scope.start("detail");
    const generation = scope.start("analysis");
    scope.select("");
    expect(scope.isCurrent(detail)).toBe(false);
    expect(scope.isCurrent(generation)).toBe(false);
  });
});
