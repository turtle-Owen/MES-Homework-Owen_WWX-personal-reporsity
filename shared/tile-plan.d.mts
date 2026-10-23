export type Tile = { x: number; y: number; width: number; height: number };
export function tilePlan(
  width: number,
  height: number,
  edge?: number,
  overlap?: number,
): Tile[];
