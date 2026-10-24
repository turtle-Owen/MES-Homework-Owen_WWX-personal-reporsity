export function imageHeader(bytes: Uint8Array | ArrayBuffer): {
  mime: string;
  width: number;
  height: number;
};
