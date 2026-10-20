import { describe, expect, it } from "vitest";
import { teachingContent } from "./teaching-content.mjs";

describe("teaching model content boundary", () => {
  it("allows absent optional sections and removes unknown fields recursively", () => {
    expect(
      teachingContent({ brief: "ok", unknown: { unsafe: true } }, "class"),
    ).toEqual({ brief: "ok" });
    expect(teachingContent({ brief: "ok" }, "student")).toEqual({
      brief: "ok",
    });
    expect(
      teachingContent(
        { boardPlan: { title: "lesson", unknown: "drop" } },
        "class",
      ),
    ).toEqual({ boardPlan: { title: "lesson" } });
    expect(
      teachingContent(
        JSON.parse('{"brief":"ok","__proto__":{"polluted":true}}'),
        "class",
      ),
    ).toEqual({ brief: "ok" });
  });
  it("validates a complete classroom result without mutating the model object", () => {
    const source = {
      brief: "summary",
      keyFindings: [{ title: "finding", detail: "evidence", action: "act" }],
      boardPlan: {
        title: "lesson",
        duration: "45 min",
        goal: "learn",
        steps: [
          {
            minutes: 5,
            title: "start",
            teacher: "explain",
            students: "answer",
          },
        ],
        blackboard: ["formula"],
      },
      questionReviews: [
        {
          number: 1,
          knowledge: "topic",
          whyWrong: "reason",
          explainToStudents: "explain",
          boardWork: "write",
          quickCheck: "check",
          wrongStudents: ["student"],
        },
      ],
      reasonInsights: [
        {
          id: "concept",
          label: "concept",
          diagnosis: "diagnosis",
          teachingMove: "correct",
          followUp: "practice",
        },
      ],
      groups: [
        {
          name: "group",
          studentNames: ["student"],
          strategy: "practice",
          materials: "paper",
        },
      ],
      homework: {
        title: "homework",
        targets: ["topic"],
        items: [
          {
            prompt: "question",
            source: "1",
            difficulty: "easy",
            expectation: "correct",
          },
        ],
      },
      teacherScript: "hello",
    };
    const snapshot = JSON.stringify(source);
    const output = teachingContent(source, "class");
    expect(output).toEqual(source);
    expect(output.boardPlan).not.toBe(source.boardPlan);
    expect(JSON.stringify(source)).toBe(snapshot);
  });
  it("validates a complete individual diagnosis", () => {
    const source = {
      brief: "summary",
      diagnosis: "detail",
      weakKnowledge: [
        {
          title: "topic",
          questionNumbers: [1, 2],
          evidence: "evidence",
          mastery: "learning",
        },
      ],
      causes: [{ label: "concept", detail: "reason", fix: "practice" }],
      actions: [
        { when: "today", what: "practice", who: "student", check: "quiz" },
      ],
      practice: [
        {
          prompt: "question",
          knowledge: "topic",
          answer: "answer",
          difficulty: "easy",
        },
      ],
      encouragement: "keep going",
      parentNote: "note",
    };
    expect(teachingContent(source, "student")).toEqual(source);
  });
  it.each(["keyFindings", "questionReviews", "reasonInsights", "groups"])(
    "rejects class array type confusion in %s",
    (field) => {
      for (const value of ["oops", {}, null, [null], ["oops"], [{}]]) {
        expect(() => teachingContent({ [field]: value }, "class")).toThrow(
          `class.${field}`,
        );
      }
    },
  );
  it.each(["weakKnowledge", "causes", "actions", "practice"])(
    "rejects student array type confusion in %s",
    (field) => {
      for (const value of ["oops", {}, null, [null], ["oops"], [{}]]) {
        expect(() => teachingContent({ [field]: value }, "student")).toThrow(
          `student.${field}`,
        );
      }
    },
  );
  it("rejects nested arrays, React-invalid object text, and malformed numeric fields", () => {
    const badClass = [
      { brief: { message: "object child" } },
      { boardPlan: "string instead of object" },
      { boardPlan: { steps: "oops" } },
      { boardPlan: { blackboard: [{ text: "oops" }] } },
      {
        boardPlan: {
          steps: [{ minutes: "5", title: "t", teacher: "t", students: "s" }],
        },
      },
      {
        boardPlan: {
          steps: [
            { minutes: Infinity, title: "t", teacher: "t", students: "s" },
          ],
        },
      },
      {
        groups: [
          { name: "g", strategy: "s", materials: "m", studentNames: "oops" },
        ],
      },
      { homework: { title: "h", targets: [5] } },
      {
        homework: {
          title: "h",
          items: [
            { prompt: {}, source: "1", difficulty: "e", expectation: "x" },
          ],
        },
      },
    ];
    for (const value of badClass)
      expect(() => teachingContent(value, "class")).toThrow("教学分析格式无效");
    for (const value of ["1", 0, -1, 1.2, NaN]) {
      expect(() =>
        teachingContent(
          {
            weakKnowledge: [
              {
                title: "t",
                evidence: "e",
                mastery: "m",
                questionNumbers: [value],
              },
            ],
          },
          "student",
        ),
      ).toThrow("questionNumbers[0]");
    }
  });
  it("rejects non-object roots and unsupported scope", () => {
    for (const value of [null, [], "text", 1, true])
      expect(() => teachingContent(value, "class")).toThrow("教学分析格式无效");
    expect(() => teachingContent({}, "unknown")).toThrow("范围无效");
  });
});
