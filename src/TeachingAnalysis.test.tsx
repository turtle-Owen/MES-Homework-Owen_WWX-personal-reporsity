import { beforeEach, describe, expect, it, vi } from "vitest";

// Execute the actual component callbacks with deterministic hook state and
// deferred API responses; this intentionally does not pretend to be a browser.
const hooks = vi.hoisted(() => ({
  slots: [] as any[],
  cursor: 0,
  effects: [] as (() => void)[],
}));
vi.mock("react", async (original) => {
  const react = await original<typeof import("react")>();
  return {
    ...react,
    useState(initial: any) {
      const index = hooks.cursor++;
      if (!(index in hooks.slots)) hooks.slots[index] = initial;
      return [
        hooks.slots[index],
        (next: any) => {
          hooks.slots[index] =
            typeof next === "function" ? next(hooks.slots[index]) : next;
        },
      ];
    },
    useRef(initial: any) {
      const index = hooks.cursor++;
      return (hooks.slots[index] ??= { current: initial });
    },
    useMemo(fn: () => any) {
      return fn();
    },
    useEffect(fn: () => void, dependencies: any[]) {
      const index = hooks.cursor++;
      const previous = hooks.slots[index];
      if (!previous || dependencies.some((value, i) => value !== previous[i])) {
        hooks.slots[index] = dependencies;
        hooks.effects.push(fn);
      }
    },
  };
});
vi.mock("./analysis-api", async (original) => ({
  ...(await original<typeof import("./analysis-api")>()),
  listTeachingAssignments: vi.fn(),
  loadTeachingAnalysis: vi.fn(),
  generateClassAnalysis: vi.fn(),
  mistakesToAssignment: vi.fn(),
}));
import { TeachingAnalysis } from "./TeachingAnalysis";
import {
  listTeachingAssignments,
  loadTeachingAnalysis,
  generateClassAnalysis,
} from "./analysis-api";
function deferred() {
  let resolve!: (value: any) => void;
  const promise = new Promise<any>((r) => {
    resolve = r;
  });
  return { promise, resolve };
}
function nodes(tree: any): any[] {
  if (!tree || typeof tree !== "object") return [];
  if (Array.isArray(tree)) return tree.flatMap(nodes);
  return [tree, ...nodes(tree.props?.children)];
}
function render() {
  hooks.cursor = 0;
  const tree = TeachingAnalysis({ notify: vi.fn() });
  hooks.effects.splice(0).forEach((effect) => effect());
  return nodes(tree);
}
async function flush() {
  for (let i = 0; i < 6; i++) await Promise.resolve();
}
const response = (id: string) => ({
  stats: {
    assignment: { id, title: id },
    overview: { mistakeCount: 0 },
    highFrequencyQuestions: [],
  },
  analysis: null,
  focus: [],
});
const select = (tree: any[], id: string) =>
  tree
    .find((n) => n.type === "select")
    .props.onChange({ target: { value: id } });
beforeEach(() => {
  hooks.slots = [];
  hooks.cursor = 0;
  hooks.effects = [];
  vi.resetAllMocks();
  vi.mocked(listTeachingAssignments).mockResolvedValue([
    { id: "A", title: "A" },
    { id: "B", title: "B" },
  ] as any);
});
describe("TeachingAnalysis actual callbacks", () => {
  it("keeps B selected and displayed after A's delayed detail returns", async () => {
    const a = deferred(),
      b = deferred();
    vi.mocked(loadTeachingAnalysis).mockImplementation((id) =>
      id === "A" ? a.promise : b.promise,
    );
    render();
    await flush();
    let tree = render(); // launch A
    select(tree, "B");
    render();
    b.resolve(response("B"));
    await flush();
    a.resolve(response("A"));
    await flush();
    tree = render();
    expect(tree.find((n) => n.type === "select").props.value).toBe("B");
    expect(hooks.slots[2].assignment.id).toBe("B");
  });
  it("discards A generation and immediately clears A data on switching to B", async () => {
    const generation = deferred();
    vi.mocked(loadTeachingAnalysis).mockImplementation(
      async (id) => response(id) as any,
    );
    vi.mocked(generateClassAnalysis).mockReturnValue(generation.promise);
    render();
    await flush();
    render();
    await flush();
    let tree = render();
    const generate = tree.find(
      (n) =>
        n.props?.onClick && n.props.children?.includes?.("生成 AI 教学方案"),
    );
    expect(generate).toBeTruthy();
    const pending = generate.props.onClick();
    select(tree, "B");
    expect(hooks.slots[2]).toBeNull();
    expect(hooks.slots[9]).toEqual([]); // picked question numbers
    render();
    await flush();
    generation.resolve({
      ...response("A"),
      analysis: { content: { brief: "A" } },
    });
    await pending;
    tree = render();
    expect(hooks.slots[2].assignment.id).toBe("B");
    expect(hooks.slots[3]).toBeNull();
    expect(tree.find((n) => n.type === "select").props.value).toBe("B");
  });
});
