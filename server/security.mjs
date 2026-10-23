import fs from "node:fs";
import dns from "node:dns";
import net from "node:net";

// Defense-in-depth for SSRF: literal origin allowlisting alone can be bypassed
// through DNS rebinding or by repointing an approved domain at internal
// addresses (e.g. cloud metadata at 169.254.169.254). Resolve the hostname and
// reject private, loopback, link-local and other non-globally-routable targets
// immediately before the request leaves the process.
export function isPrivateAddress(address) {
  if (typeof address !== "string" || !net.isIP(address)) return true;
  if (net.isIPv4(address)) {
    const [a, b] = address.split(".").map(Number);
    // 0.x, 10.x, 100.64/10 CGNAT, 127 loopback, 169.254 link-local,
    // 172.16/12, 192.0/24 & 192.0.2/24 discard/TEST-NET, 192.168/16,
    // 198.18 benchmarking, 198.51/198.51.1 TEST-NET, 203.0.113 TEST-NET,
    // 224+ multicast/reserved, 240+ reserved.
    if (a === 0 || a === 10 || a === 127) return true;
    if (a === 169 && b === 254) return true;
    if (a === 172 && b >= 16 && b <= 31) return true;
    if (a === 192 && (b === 0 || b === 168)) return true;
    if (a === 198 && (b === 18 || b === 19)) return true;
    if (a >= 224) return true;
    return false;
  }
  const mapped = address.match(/^::ffff:(\d+\.\d+\.\d+\.\d+)$/i);
  if (mapped) return isPrivateAddress(mapped[1]);
  const lower = address.toLowerCase();
  if (lower === "::" || lower === "::1") return true;
  const hextets = address.split("%")[0].split(":");
  const first = parseInt(hextets[0] || "0", 16) || 0;
  // fc00::/7 unique-local, fe80::/10 link-local, 2001:db8 documentation,
  // 5f00::/8 and 2002::/8 & 3ffe::/16 special-purpose/reserved.
  if ((first & 0xfe00) === 0xfc00) return true;
  if ((first & 0xffc0) === 0xfe80) return true;
  if (first === 0x2001 && (hextets[1] || "").toLowerCase() === "db8")
    return true;
  if (first === 0x5f00 || first === 0x2002 || first === 0x3ffe) return true;
  return false;
}

const defaultLookup = (hostname) =>
  dns.promises
    .lookup(hostname, { all: true, verbatim: true })
    .then((rows) => rows.map((row) => row.address));

export async function assertPublicProviderEndpoint(endpoint, lookup) {
  const url = new URL(endpoint);
  if (!net.isIP(url.hostname)) {
    let addresses;
    try {
      addresses = await (lookup || defaultLookup)(url.hostname);
    } catch {
      throw new Error("模型接口域名无法解析，出于安全考虑拒绝访问内网地址");
    }
    if (!addresses.length)
      throw new Error("模型接口域名无法解析，出于安全考虑拒绝访问内网地址");
    if (addresses.some(isPrivateAddress))
      throw new Error("模型接口不得指向内网或保留地址");
  } else if (isPrivateAddress(url.hostname)) {
    throw new Error("模型接口不得指向内网或保留地址");
  }
  return url.href;
}

export function providerEndpoint(
  endpoint,
  preset,
  extraOrigins = process.env.PROVIDER_ALLOWED_ORIGINS || "",
) {
  const url = new URL(endpoint);
  const allowed = [
    new URL(preset.endpoint).origin,
    ...extraOrigins
      .split(",")
      .filter(Boolean)
      .map((value) => new URL(value.trim()).origin),
  ];
  if (
    url.protocol !== "https:" ||
    url.username ||
    url.password ||
    url.hash ||
    !allowed.includes(url.origin)
  )
    throw new Error(
      "模型接口须使用厂商 HTTPS 域名；自定义域名须由部署管理员加入 PROVIDER_ALLOWED_ORIGINS",
    );
  return url.href;
}

export function sameOriginWrites(req, res, next) {
  res.setHeader("X-Content-Type-Options", "nosniff");
  res.setHeader("Referrer-Policy", "same-origin");
  res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  if (req.path.startsWith("/api/")) res.setHeader("Cache-Control", "no-store");
  if (!["GET", "HEAD", "OPTIONS"].includes(req.method)) {
    // Forwarded headers are attacker-controlled when the origin is reachable directly.
    // A split-origin deployment must configure its public origin explicitly.
    const origin = req.get("origin");
    const allowed =
      process.env.APP_ORIGIN || `${req.protocol}://${req.get("host")}`;
    if (
      req.get("sec-fetch-site") === "cross-site" ||
      (origin && origin !== allowed)
    )
      return res.status(403).json({ error: "不允许跨站写入" });
  }
  next();
}

export function authRateLimit() {
  const attempts = new Map();
  const windowMs = 10 * 60 * 1000;
  return (req, res, next) => {
    const now = Date.now();
    for (const [key, entry] of attempts)
      if (entry.until <= now) attempts.delete(key);
    const key = req.ip;
    let entry = attempts.get(key);
    if (!entry) {
      if (attempts.size >= 10000)
        return res.status(429).json({ error: "请求过于频繁" });
      entry = { count: 0, until: now + windowMs };
      attempts.set(key, entry);
    }
    if (++entry.count > 30) {
      res.setHeader("Retry-After", Math.ceil((entry.until - now) / 1000));
      return res.status(429).json({ error: "登录或注册过于频繁，请稍后重试" });
    }
    next();
  };
}

export function validPassword(value) {
  return (
    typeof value === "string" &&
    value.length >= 8 &&
    Buffer.byteLength(value) <= 1024
  );
}

// Never trust multipart MIME headers or original filename extensions.
export function uploadType(buffer, claimed) {
  const starts = (bytes) =>
    buffer.subarray(0, bytes.length).equals(Buffer.from(bytes));
  if (claimed === "image/jpeg" && starts([0xff, 0xd8, 0xff])) return ".jpg";
  if (claimed === "image/png" && starts([137, 80, 78, 71, 13, 10, 26, 10]))
    return ".png";
  if (
    claimed === "image/webp" &&
    buffer.subarray(0, 4).toString() === "RIFF" &&
    buffer.subarray(8, 12).toString() === "WEBP"
  )
    return ".webp";
  if (
    claimed === "application/pdf" &&
    buffer.subarray(0, 5).toString() === "%PDF-"
  )
    return ".pdf";
  if (
    claimed === "application/msword" &&
    starts([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1])
  )
    return ".doc";
  if (
    claimed ===
      "application/vnd.openxmlformats-officedocument.wordprocessingml.document" &&
    starts([0x50, 0x4b, 0x03, 0x04])
  )
    return ".docx";
  throw new Error("文件内容与支持的文件类型不符");
}

export function validateUploads(req, res, next) {
  const uploads = req.file ? [req.file] : req.files || [];
  const cleanup = () =>
    uploads.forEach((file) => fs.rmSync(file.path, { force: true }));
  res.once("finish", cleanup);
  res.once("close", cleanup);
  try {
    const configured = Number(process.env.UPLOAD_TOTAL_MB);
    const maxBytes =
      (Number.isSafeInteger(configured) && configured > 0 ? configured : 50) *
      1024 *
      1024;
    if (uploads.reduce((sum, file) => sum + file.size, 0) > maxBytes)
      throw Object.assign(
        new Error("整份上传超过总大小限制，请拆分或压缩后上传"),
        { status: 413 },
      );
    for (const file of uploads) {
      const fd = fs.openSync(file.path, "r");
      const header = Buffer.alloc(16);
      try {
        fs.readSync(fd, header, 0, header.length, 0);
      } finally {
        fs.closeSync(fd);
      }
      file.safeExtension = uploadType(header, file.mimetype);
    }
    next();
  } catch (error) {
    cleanup();
    res.status(error.status || 400).json({ error: error.message });
  }
}
