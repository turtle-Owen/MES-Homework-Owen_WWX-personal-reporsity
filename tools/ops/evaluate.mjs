import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
// Offline comparison only: teacher labels and model output are supplied by the operator.
export function evaluate(samples) {
  if (!Array.isArray(samples) || !samples.length)
    throw new Error("评测集不能为空");
  let questions = 0,
    matched = 0,
    exact = 0,
    verdicts = 0,
    scoreError = 0;
  for (const sample of samples) {
    if (
      !Array.isArray(sample.expected) ||
      !sample.expected.length ||
      !Array.isArray(sample.actual)
    )
      throw new Error("样本必须包含 expected 和 actual 题目数组");
    const ids = new Set(sample.expected.map((q) => q.questionId));
    if (
      ids.size !== sample.expected.length ||
      new Set(sample.actual.map((q) => q.questionId)).size !==
        sample.actual.length
    )
      throw new Error("样本题号重复");
    if (sample.actual.some((q) => !ids.has(q.questionId)))
      throw new Error("识别结果包含非正式题号");
    const byId = new Map(sample.actual.map((q) => [q.questionId, q]));
    for (const expected of sample.expected) {
      if (
        typeof expected.questionId !== "string" ||
        typeof expected.isCorrect !== "boolean" ||
        !Number.isFinite(expected.score) ||
        typeof expected.studentAnswer !== "string"
      )
        throw new Error("教师标注无效");
      questions++;
      const actual = byId.get(expected.questionId);
      if (!actual) continue;
      if (
        typeof actual.studentAnswer !== "string" ||
        typeof actual.isCorrect !== "boolean" ||
        !Number.isFinite(actual.score)
      )
        throw new Error("识别结果字段无效");
      matched++;
      const normalize = (text) => text.normalize("NFC").replace(/\s+/gu, "");
      if (normalize(actual.studentAnswer) === normalize(expected.studentAnswer))
        exact++;
      if (actual.isCorrect === expected.isCorrect) verdicts++;
      scoreError += Math.abs(actual.score - expected.score);
    }
  }
  return {
    samples: samples.length,
    questions,
    matched,
    missing: questions - matched,
    alignmentRate: matched / questions,
    exactAnswerRate: exact / questions,
    verdictAccuracy: verdicts / questions,
    scoreMAEOnMatched: matched ? scoreError / matched : null,
  };
}
if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href
) {
  try {
    if (!process.argv[2])
      throw new Error("用法：node tools/ops/evaluate.mjs <匿名化评测 JSON>");
    console.log(
      JSON.stringify(
        evaluate(JSON.parse(fs.readFileSync(process.argv[2], "utf8"))),
        null,
        2,
      ),
    );
  } catch (e) {
    console.error(e.message);
    process.exitCode = 1;
  }
}
