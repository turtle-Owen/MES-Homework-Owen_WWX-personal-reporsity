import { describe, expect, it } from "vitest";
import {
  normalizeReview,
  practiceGrade,
  publicPractice,
  studentAnalysis,
} from "./grading.mjs";
const questions = [
  { id: "q1", score: 5, knowledge_id: "k1" },
  { id: "q2", score: 10, knowledge_id: null },
];
describe("teacher review invariants", () => {
  it("derives a bounded total and canonical knowledge even when the client supplies other values", () => {
    const result = normalizeReview(
      [
        { questionId: "q1", isCorrect: true, score: 5, knowledgeId: "foreign" },
        { questionId: "q2", isCorrect: false, score: 3 },
      ],
      questions,
    );
    expect(result.score).toBe(8);
    expect(result.items[0].knowledgeId).toBe("k1");
  });
  it.each([-1, 6, Infinity, NaN, "5", null])(
    "rejects invalid item score %s",
    (score) => {
      expect(() =>
        normalizeReview(
          [
            { questionId: "q1", isCorrect: true, score },
            { questionId: "q2", isCorrect: true },
          ],
          questions,
        ),
      ).toThrow();
    },
  );
  it("rejects correct zero-score and incorrect full-score verdicts", () => {
    for (const [isCorrect, score] of [
      [true, 0],
      [false, 5],
    ])
      expect(() =>
        normalizeReview(
          [
            { questionId: "q1", isCorrect, score },
            { questionId: "q2", isCorrect: true },
          ],
          questions,
        ),
      ).toThrow();
  });
  it("supports explicit zero-point questions without inventing correctness", () => {
    expect(
      normalizeReview(
        [{ questionId: "zero", isCorrect: false, score: 0 }],
        [{ id: "zero", score: 0 }],
      ).items[0].isCorrect,
    ).toBe(false);
  });
  it("requires each canonical question exactly once", () => {
    expect(() =>
      normalizeReview(
        [
          { questionId: "q1", isCorrect: true },
          { questionId: "q1", isCorrect: false },
        ],
        questions,
      ),
    ).toThrow();
    expect(() =>
      normalizeReview(
        [
          { questionId: "foreign", isCorrect: true },
          { questionId: "q2", isCorrect: true },
        ],
        questions,
      ),
    ).toThrow();
  });
});
describe("student practice boundaries", () => {
  it("strips answers, explanations and unexpected fields before and after review", () => {
    const privateQuestions = [
      {
        prompt: "question",
        answer: "answer",
        explanation: "solution",
        metadata: { secret: "hidden" },
      },
    ];
    expect(publicPractice(privateQuestions)).toEqual([{ prompt: "question" }]);
    expect(
      studentAnalysis({
        items: [{ questionId: "q1", practiceQuestions: privateQuestions }],
      }).items[0].practiceQuestions,
    ).toEqual([{ prompt: "question" }]);
  });
  it.each([
    { passed: "false", results: [{ correct: "false" }] },
    { passed: true, results: [{ correct: "true" }] },
    { passed: 1, results: [{ correct: true }] },
    { passed: true, results: {} },
    { passed: true, results: [] },
  ])(
    "rejects malformed model verdicts rather than casting truthiness",
    (graded) => expect(() => practiceGrade(graded, 1)).toThrow(),
  );
  it("requires all strict boolean verdicts to pass and exposes only grading fields", () => {
    expect(
      practiceGrade(
        {
          passed: true,
          results: [{ correct: false, feedback: "retry", answer: "hidden" }],
        },
        1,
      ),
    ).toEqual({
      passed: false,
      results: [{ correct: false, feedback: "retry" }],
    });
    expect(
      practiceGrade({ passed: true, results: [{ correct: true }] }, 1).passed,
    ).toBe(true);
  });
});
