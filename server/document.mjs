import { createCanvas, DOMMatrix, ImageData, Path2D } from "@napi-rs/canvas";
import fs from "node:fs";

globalThis.DOMMatrix ||= DOMMatrix;
globalThis.ImageData ||= ImageData;
globalThis.Path2D ||= Path2D;

export async function documentImages(filePath, mimeType, maxPages = 10) {
  const source = fs.readFileSync(filePath);
  if (mimeType !== "application/pdf") {
    return [{ mimeType, base64: source.toString("base64") }];
  }

  const { getDocument } = await import("pdfjs-dist/legacy/build/pdf.mjs");
  const loading = getDocument({
    data: new Uint8Array(source),
    disableWorker: true,
    useSystemFonts: true,
    isEvalSupported: false,
  });
  try {
    const pdf = await loading.promise;
    if (pdf.numPages > maxPages) {
      throw new Error(
        `PDF 共 ${pdf.numPages} 页，单次最多识别 ${maxPages} 页，请拆分后上传`,
      );
    }
    const pages = [];
    let encodedBytes = 0;
    for (let pageNumber = 1; pageNumber <= pdf.numPages; pageNumber += 1) {
      const page = await pdf.getPage(pageNumber);
      const viewport = page.getViewport({ scale: 2 });
      if (
        !Number.isFinite(viewport.width) ||
        !Number.isFinite(viewport.height) ||
        viewport.width <= 0 ||
        viewport.height <= 0 ||
        viewport.width > 10000 ||
        viewport.height > 10000 ||
        viewport.width * viewport.height > 16000000
      )
        throw new Error("PDF 页面尺寸过大，请缩小后上传");
      const canvas = createCanvas(
        Math.ceil(viewport.width),
        Math.ceil(viewport.height),
      );
      const canvasContext = canvas.getContext("2d");
      try {
        await page.render({ canvas, canvasContext, viewport }).promise;
        const base64 = canvas.toBuffer("image/jpeg", 88).toString("base64");
        encodedBytes += base64.length;
        if (encodedBytes > 44 * 1024 * 1024)
          throw new Error("PDF 解析图片合计过大，请拆分或压缩后重试");
        pages.push({ mimeType: "image/jpeg", base64 });
      } finally {
        // Release native pixel storage promptly rather than waiting for GC.
        canvas.width = 1;
        canvas.height = 1;
        page.cleanup();
      }
    }
    if (typeof pdf.cleanup === "function") await pdf.cleanup();
    return pages;
  } finally {
    await loading.destroy();
  }
}

export { cleanModelJson } from "./model-json.mjs";
