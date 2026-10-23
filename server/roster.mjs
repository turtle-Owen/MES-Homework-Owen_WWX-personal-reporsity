import fs from "node:fs";
import readXlsxFile from "read-excel-file/node";
export function parseCsv(text) {
  const rows = [];
  let row = [],
    cell = "",
    quoted = false;
  for (let i = 0; i < text.length; i += 1) {
    const char = text[i];
    if (char === '"' && quoted && text[i + 1] === '"') {
      cell += '"';
      i += 1;
    } else if (char === '"') quoted = !quoted;
    else if (char === "," && !quoted) {
      row.push(cell);
      cell = "";
    } else if ((char === "\n" || char === "\r") && !quoted) {
      if (char === "\r" && text[i + 1] === "\n") i += 1;
      row.push(cell);
      if (row.some((value) => value.trim())) rows.push(row);
      row = [];
      cell = "";
    } else cell += char;
  }
  row.push(cell);
  if (row.some((value) => value.trim())) rows.push(row);
  return rows;
}

export async function readRoster(file) {
  const table = file.isCsv
    ? parseCsv(
        (await fs.promises.readFile(file.file_path, "utf8")).replace(
          /^\uFEFF/,
          "",
        ),
      )
    : await readXlsxFile(file.file_path);
  if (JSON.stringify(table).length > 1000000)
    throw new Error("学生表格内容过大，请精简后重试");
  return table;
}
