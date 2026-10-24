// Pure geometry, shared by browser worker and server. No client supplied coordinates.
export function tilePlan(width, height, edge = 1300, overlap = 195) {
  if (
    ![width, height].every(
      (n) => Number.isSafeInteger(n) && n > 0 && n <= 8192,
    ) ||
    width * height > 24_000_000
  )
    throw new Error("照片过大，最多 2400 万像素、单边 8192 像素，请分次拍摄");
  const axis = (size) => {
    const positions = [0];
    while (positions.at(-1) + edge < size)
      positions.push(Math.min(positions.at(-1) + edge - overlap, size - edge));
    return positions;
  };
  return axis(height).flatMap((y) =>
    axis(width).map((x) => ({
      x,
      y,
      width: Math.min(edge, width),
      height: Math.min(edge, height),
    })),
  );
}
