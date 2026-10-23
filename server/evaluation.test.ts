import { it, expect } from "vitest";
import { evaluate } from "../tools/ops/evaluate.mjs";
it("counts missing questions against accuracy and separates grading from recognition", () => {
  const q = {
    questionId: "q",
    studentAnswer: "2 A",
    score: 5,
    isCorrect: true,
  };
  const missing = { ...q, questionId: "missing" };
  expect(
    evaluate([
      {
        expected: [q, missing],
        actual: [{ ...q, studentAnswer: "2A", score: 0, isCorrect: false }],
      },
    ]),
  ).toMatchObject({
    alignmentRate: 0.5,
    exactAnswerRate: 0.5,
    verdictAccuracy: 0,
    scoreMAEOnMatched: 5,
  });
  expect(() => evaluate([{ expected: [q], actual: [q, q] }])).toThrow(/重复/);
  expect(() => evaluate([])).toThrow(/不能为空/);
});
