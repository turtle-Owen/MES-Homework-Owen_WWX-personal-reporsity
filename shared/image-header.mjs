// Max 64 KiB of metadata, no decoder. Bounds resource admission, not file integrity.
export function imageHeader(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const ascii = (a, z) => String.fromCharCode(...b.subarray(a, z));
  const result = (mime, width, height) => {
    if (
      ![width, height].every(
        (n) => Number.isSafeInteger(n) && n > 0 && n <= 8192,
      ) ||
      width * height > 24_000_000
    )
      throw new Error("图片尺寸超限，最多 2400 万像素、单边 8192 像素");
    return { mime, width, height };
  };
  if (
    b.length >= 33 &&
    [137, 80, 78, 71, 13, 10, 26, 10].every((n, i) => b[i] === n) &&
    ascii(12, 16) === "IHDR"
  )
    return result("image/png", v.getUint32(16), v.getUint32(20));
  if (b.length >= 12 && b[0] === 255 && b[1] === 216) {
    let i = 2;
    while (i + 4 <= b.length) {
      if (b[i++] !== 255) throw new Error("JPEG 头部无效");
      while (i < b.length && b[i] === 255) i++;
      const marker = b[i++];
      if ([0xd8, 0xd9, 0xda].includes(marker)) break;
      if (marker === 1 || (marker >= 0xd0 && marker <= 0xd7)) continue;
      if (i + 2 > b.length) break;
      const length = v.getUint16(i);
      if (length < 2) throw new Error("JPEG 段长度无效");
      if (
        [0xc0, 0xc1, 0xc2].includes(marker) &&
        length >= 8 &&
        i + 8 <= b.length
      )
        return result("image/jpeg", v.getUint16(i + 5), v.getUint16(i + 3));
      i += length;
    }
  }
  if (b.length >= 30 && ascii(0, 4) === "RIFF" && ascii(8, 12) === "WEBP") {
    const tag = ascii(12, 16);
    if (tag === "VP8X") {
      if (b[20] & 2) throw new Error("不支持动态图");
      return result(
        "image/webp",
        (b[24] | (b[25] << 8) | (b[26] << 16)) + 1,
        (b[27] | (b[28] << 8) | (b[29] << 16)) + 1,
      );
    }
    if (tag === "VP8 " && b[23] === 0x9d && b[24] === 1 && b[25] === 0x2a)
      return result(
        "image/webp",
        v.getUint16(26, true) & 0x3fff,
        v.getUint16(28, true) & 0x3fff,
      );
    if (tag === "VP8L" && b[20] === 0x2f) {
      const bits = v.getUint32(21, true);
      return result(
        "image/webp",
        (bits & 0x3fff) + 1,
        ((bits >>> 14) & 0x3fff) + 1,
      );
    }
  }
  throw new Error("无法在 64 KiB 内识别图片尺寸，请另存为 JPEG 后重试");
}
