/// <reference lib="webworker" />
import { imageHeader } from "../shared/image-header.mjs";
import { tilePlan, type Tile } from "../shared/tile-plan.mjs";
let bitmap: ImageBitmap | null = null;
self.onmessage = async ({ data }) => {
  try {
    if (
      typeof OffscreenCanvas === "undefined" ||
      typeof OffscreenCanvas.prototype.convertToBlob !== "function" ||
      typeof createImageBitmap === "undefined"
    )
      throw new Error("当前设备不支持后台切片，请关闭切片选项使用普通上传");
    let result: unknown;
    if (data.type === "inspect") {
      const pages = [];
      for (const file of data.files as File[]) {
        if (file.size > 20 * 1024 * 1024)
          throw new Error("单张照片不得超过 20 MB");
        imageHeader(await file.slice(0, 65536).arrayBuffer());
        const picture = await createImageBitmap(file);
        try {
          tilePlan(picture.width, picture.height);
          pages.push({
            width: picture.width,
            height: picture.height,
            name: file.name,
            bytes: file.size,
          });
        } finally {
          picture.close();
        }
      }
      result = pages;
    } else if (data.type === "open") {
      bitmap?.close();
      bitmap = null;
      const picture = await createImageBitmap(data.file);
      tilePlan(picture.width, picture.height);
      bitmap = picture;
      result = true;
    } else if (data.type === "tile") {
      if (!bitmap) throw new Error("页面未加载");
      const p = data.part as Tile;
      const canvas = new OffscreenCanvas(p.width, p.height);
      const context = canvas.getContext("2d");
      if (!context) throw new Error("无法创建切片画布");
      context.fillStyle = "white";
      context.fillRect(0, 0, p.width, p.height);
      context.drawImage(
        bitmap,
        p.x,
        p.y,
        p.width,
        p.height,
        0,
        0,
        p.width,
        p.height,
      );
      result = await canvas.convertToBlob({
        type: "image/jpeg",
        quality: 0.85,
      });
      canvas.width = 1;
      canvas.height = 1;
    } else throw new Error("未知切片操作");
    self.postMessage({ id: data.id, result });
  } catch (error) {
    self.postMessage({ id: data.id, error: (error as Error).message });
  }
};
