// Model JSON is untrusted data, including when read back from historical rows.
// Mirror src/analysis-api.ts and construct fresh objects from known fields only.
const optional = (value) => ({ optional: value });
const array = (value) => ({ array: value });
const text = "text";
const number = "number";
const questionNumber = "questionNumber";
const fields = (names) =>
  Object.fromEntries(names.split(" ").map((name) => [name, text]));

const classSchema = {
  brief: optional(text),
  keyFindings: optional(array(fields("title detail action"))),
  boardPlan: optional({
    title: optional(text),
    duration: optional(text),
    goal: optional(text),
    steps: optional(
      array({ minutes: number, ...fields("title teacher students") }),
    ),
    blackboard: optional(array(text)),
  }),
  questionReviews: optional(
    array({
      number: questionNumber,
      ...fields("knowledge whyWrong explainToStudents boardWork quickCheck"),
      wrongStudents: optional(array(text)),
    }),
  ),
  reasonInsights: optional(
    array(fields("id label diagnosis teachingMove followUp")),
  ),
  groups: optional(
    array({
      ...fields("name strategy materials"),
      studentNames: array(text),
    }),
  ),
  homework: optional({
    title: text,
    targets: optional(array(text)),
    items: optional(array(fields("prompt source difficulty expectation"))),
  }),
  teacherScript: optional(text),
};
const studentSchema = {
  brief: optional(text),
  diagnosis: optional(text),
  weakKnowledge: optional(
    array({
      ...fields("title evidence mastery"),
      questionNumbers: array(questionNumber),
    }),
  ),
  causes: optional(array(fields("label detail fix"))),
  actions: optional(array(fields("when what who check"))),
  practice: optional(array(fields("prompt knowledge answer difficulty"))),
  encouragement: optional(text),
  parentNote: optional(text),
};
function invalid(path, expected) {
  throw new Error(`教学分析格式无效：${path}须为${expected}，请重新生成`);
}
function validate(value, schema, path) {
  if (schema === text) {
    if (typeof value !== "string") invalid(path, "文字");
    return value;
  }
  if (schema === number || schema === questionNumber) {
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0)
      invalid(path, "非负有限数字");
    if (
      schema === questionNumber &&
      (!Number.isSafeInteger(value) || value < 1)
    )
      invalid(path, "正整数题号");
    return value;
  }
  if (Object.hasOwn(schema, "array")) {
    if (!Array.isArray(value)) invalid(path, "数组");
    return Array.from(value, (item, index) =>
      validate(item, schema.array, `${path}[${index}]`),
    );
  }
  if (!value || typeof value !== "object" || Array.isArray(value))
    invalid(path, "对象");
  const result = {};
  for (const [key, child] of Object.entries(schema)) {
    const isOptional =
      typeof child === "object" && Object.hasOwn(child, "optional");
    if (!Object.hasOwn(value, key)) {
      if (isOptional) continue;
      invalid(`${path}.${key}`, "完整字段");
    }
    result[key] = validate(
      value[key],
      isOptional ? child.optional : child,
      `${path}.${key}`,
    );
  }
  return result;
}
export function teachingContent(raw, scope) {
  if (scope !== "class" && scope !== "student")
    throw new Error("教学分析范围无效");
  return validate(raw, scope === "class" ? classSchema : studentSchema, scope);
}
