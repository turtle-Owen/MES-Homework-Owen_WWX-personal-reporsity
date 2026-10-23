import { documentImages } from "./document.mjs";
import { extractWordDocument, isWordDocument } from "./word.mjs";
process.once("disconnect", () => process.exit(0));
process.once("message", async (workerData) => {
  try {
    if (
      workerData.length === 1 &&
      workerData[0].file_type === "application/x-mes-roster"
    ) {
      const { readRoster } = await import("./roster.mjs");
      process.send({ result: { table: await readRoster(workerData[0]) } });
      return;
    }
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
      } else if (file.file_type !== "application/pdf") {
        // Immutable photos need no decoding or worker-to-parent Base64 copy.
        images.push({ mimeType: file.file_type, filePath: file.file_path });
      } else
        images.push(
          ...(await documentImages(
            file.file_path,
            file.file_type,
            10 - images.length,
          )),
        );
      if (
        images.reduce((sum, image) => sum + (image.base64?.length || 0), 0) >
        44 * 1024 * 1024
      )
        throw new Error("解析图片合计过大，请拆分或压缩后重试");
      if (images.length > 10 || texts.join("\n").length > 1000000)
        throw new Error(
          "整份答卷最多 10 页图片，文字不得超过 100 万字符，请拆分或压缩后重试",
        );
    }
    process.send({
      result: { images, documentText: texts.join("\n"), warnings },
    });
  } catch (e) {
    process.send({ error: e.message });
  }
});
