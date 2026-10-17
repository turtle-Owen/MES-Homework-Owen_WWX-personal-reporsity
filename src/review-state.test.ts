import { describe, expect, it } from "vitest";
import {
  changeReviewGrade,
  createReviewGuard,
  normalizeReviewDetail,
  reviewRows,
  reviewScoreError,
} from "./review-state";

const questions = [
  { id: "q1", number: 1, prompt: "第一题", score: 5 },
  { id: "q2", number: 2, prompt: "第二题", score: 10 },
];
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((r) => {
    resolve = r;
  });
  return { promise, resolve };
}

describe("review selection isolation", () => {
  it("ignores a detail request that finishes after a later selection", async () => {
    const guard = createReviewGuard();
    const a = deferred<string>(),
      b = deferred<string>();
    let displayed = "";
    const select = async (id: string, request: Promise<string>) => {
      const ticket = guard.select(id);
      const result = await request;
      if (guard.isCurrent(ticket)) displayed = result;
    };
    const pendingA = select("A", a.promise);
    const pendingB = select("B", b.promise);
    b.resolve("B detail");
    await pendingB;
    a.resolve("A detail");
    await pendingA;
    expect(displayed).toBe("B detail");
  });

  it("does not apply recognition for A to selected B, including a deferred canonical GET", async () => {
    const guard = createReviewGuard();
    guard.select("A");
    const response = deferred<string>();
    const selection = guard.current();
    let detail = "B detail";
    const pending = response.promise.then((value) => {
      if (guard.isCurrent(selection)) detail = value;
    });
    guard.select("B");
    response.resolve("A recognized");
    await pending;
    expect(detail).toBe("B detail");
  });

  it("rejects an older response even when A is selected again", () => {
    const guard = createReviewGuard();
    const old = guard.select("A");
    guard.select("B");
    guard.select("A");
    expect(guard.isCurrent(old)).toBe(false);
  });

  it("serializes operations per submission without clearing another submission's busy state", () => {
    const guard = createReviewGuard();
    const a = guard.startOperation("A")!;
    expect(guard.startOperation("A")).toBeNull();
    const b = guard.startOperation("B")!;
    expect(guard.finishOperation(a)).toBe(true);
    expect(guard.startOperation("B")).toBeNull();
    expect(guard.finishOperation(a)).toBe(false);
    expect(guard.finishOperation(b)).toBe(true);
  });
});

describe("review grading", () => {
  it("matches items by question ID and sorts by actual question number", () => {
    const result = reviewRows([...questions].reverse(), [
      { questionId: "q2", studentAnswer: "second" },
      { questionId: "q1", studentAnswer: "first" },
    ]);
    expect(
      result.rows.map(({ question, item }) => [
        question.number,
        item.studentAnswer,
      ]),
    ).toEqual([
      [1, "first"],
      [2, "second"],
    ]);
    expect(
      reviewRows(questions, [{ questionId: "q1" }, { questionId: "q1" }]).error,
    ).toBeTruthy();
    expect(
      reviewRows(questions, [{ questionId: "q1" }, { questionId: "foreign" }])
        .error,
    ).toBeTruthy();
  });

  it("awards full marks when marked correct, and clears full marks when marked incorrect", () => {
    const correct = changeReviewGrade(
      { questionId: "q1", score: 0 },
      questions[0],
      { isCorrect: true },
    );
    expect(correct).toMatchObject({ isCorrect: true, score: 5 });
    expect(
      changeReviewGrade(correct, questions[0], { isCorrect: false }),
    ).toMatchObject({ isCorrect: false, score: 0 });
    expect(
      changeReviewGrade({ questionId: "q1", score: 2 }, questions[0], {
        isCorrect: false,
      }).score,
    ).toBe(2);
  });

  it("updates the verdict when editing scores and rejects blank, excessive, or inconsistent scores", () => {
    const item = { questionId: "q1", score: 0, isCorrect: false };
    expect(
      changeReviewGrade(item, questions[0], { score: "5" }).isCorrect,
    ).toBe(true);
    expect(
      changeReviewGrade(item, questions[0], { score: "2.5" }).isCorrect,
    ).toBe(false);
    for (const score of ["", "999999", "NaN", "-1"]) {
      expect(
        reviewScoreError(
          [questions[0]],
          [changeReviewGrade(item, questions[0], { score })],
        ),
      ).toBeTruthy();
    }
    expect(
      reviewScoreError(
        [questions[0]],
        [{ ...item, score: 2, isCorrect: true }],
      ),
    ).toBeTruthy();
    expect(reviewScoreError([questions[0]], [{ ...item, score: 2 }])).toBe("");
  });

  it("allows zero-point questions to retain an independent verdict", () => {
    const question = { ...questions[0], score: 0 };
    for (const isCorrect of [true, false]) {
      const item = changeReviewGrade({ questionId: "q1" }, question, {
        isCorrect,
      });
      expect(item.score).toBe(0);
      expect(changeReviewGrade(item, question, { score: "0" }).isCorrect).toBe(
        isCorrect,
      );
      expect(reviewScoreError([question], [item])).toBe("");
    }
  });

  it("normalizes initial model scores without trusting string booleans or array order", () => {
    const detail = normalizeReviewDetail({
      questions,
      analysis: {
        items: [
          { questionId: "q2", isCorrect: true, score: -99 },
          {
            questionId: "q1",
            isCorrect: "false" as unknown as boolean,
            score: 5,
          },
        ],
      },
    });
    expect(detail.analysis.items).toEqual([
      { questionId: "q2", isCorrect: true, score: 10 },
      { questionId: "q1", isCorrect: false, score: 0 },
    ]);
  });
});

describe("malformed model results remain recoverable", () => {
  it.each([{}, "not-array", [null], [false], null])(
    "keeps an invalid OCR result available for retry",
    (items) => {
      const detail = normalizeReviewDetail({
        questions: [{ id: "q1", number: 1, prompt: "q", score: 5 }],
        analysis: { items: items as any },
      });
      expect(detail.analysis.items).toEqual([]);
      expect(
        reviewRows(detail.questions, detail.analysis.items).error,
      ).toBeTruthy();
    },
  );
  it("rejects malformed rows without throwing during render", () => {
    expect(reviewRows([], {} as any).error).toBeTruthy();
    expect(
      reviewRows([{ id: "q1", number: 1, prompt: "q", score: 5 }], [
        null,
      ] as any).error,
    ).toBeTruthy();
  });
});
