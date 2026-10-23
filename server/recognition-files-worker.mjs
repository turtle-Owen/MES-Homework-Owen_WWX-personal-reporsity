import { workerData, parentPort } from "node:worker_threads";
import { documentImages } from "./document.mjs";
import { extractWordDocument, isWordDocument } from "./word.mjs";
try {
  const images = [],
    texts = [],
    warnings = [];
  for (const [index, file] of workerData.entries()) {
    if (isWordDocument(file.file_type, file.file_path)) {
      const word = await extractWordDocument(
        file.file_path,
        file.file_type,
        file.file_path,
      );
      texts.push(`附件 ${index + 1}：${word.text}`);
      images.push(...word.images);
      warnings.push(...word.warnings);
    } else
      images.push(
        ...(await documentImages(
          file.file_path,
          file.file_type,
          10 - images.length,
        )),
      );
    if (images.length > 10 || texts.join("\n").length > 1000000)
      throw new Error(
        "整份答卷最多 10 页图片，文字不得超过 100 万字符，请拆分或压缩后重试",
      );
  }
  parentPort.postMessage({
    result: { images, documentText: texts.join("\n"), warnings },
  });
} catch (e) {
  parentPort.postMessage({ error: e.message });
}
