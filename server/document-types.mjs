export const DOCX =
  "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
export const DOC = "application/msword";
export function isWordDocument(mimeType, fileName = "") {
  const lower = fileName.toLowerCase();
  return (
    mimeType === DOCX ||
    mimeType === DOC ||
    lower.endsWith(".docx") ||
    lower.endsWith(".doc")
  );
}
