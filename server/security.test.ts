import { describe, expect, it } from "vitest";
import {
  providerEndpoint,
  uploadType,
  validPassword,
  isPrivateAddress,
  assertPublicProviderEndpoint,
} from "./security.mjs";
import { providers } from "./providers.mjs";

describe("provider credential destination", () => {
  it("allows the selected vendor and deployment-approved HTTPS origins", () => {
    expect(
      providerEndpoint(providers.openai.endpoint, providers.openai, ""),
    ).toBe(providers.openai.endpoint);
    expect(
      providerEndpoint(
        "https://gateway.example/v1/chat",
        providers.openai,
        "https://gateway.example",
      ),
    ).toBe("https://gateway.example/v1/chat");
  });
  it.each([
    "http://api.openai.com/v1/chat",
    "https://127.0.0.1/admin",
    "https://169.254.169.254/latest/meta-data/",
    "https://api.openai.com.evil.example/v1/chat",
    "https://evil.example/",
    "https://api.openai.com:8443/v1/chat",
    "https://user:pass@api.openai.com/v1/chat",
    "https://api.openai.com/v1/chat#fragment",
    "file:///etc/passwd",
  ])("rejects untrusted destination %s", (endpoint) => {
    expect(() => providerEndpoint(endpoint, providers.openai, "")).toThrow();
  });
});

describe("SSRF DNS resolution guard", () => {
  it.each([
    "127.0.0.1",
    "10.0.0.5",
    "169.254.169.254",
    "172.16.0.1",
    "192.168.1.1",
    "0.0.0.0",
    "::1",
    "fd00::1",
    "fe80::1",
    "::ffff:127.0.0.1",
    "not-an-ip",
  ])("classifies %s as private or invalid", (address) => {
    expect(isPrivateAddress(address)).toBe(true);
  });
  it.each(["93.184.216.34", "2606:4700:4400::ac40:93bc"])(
    "classifies %s as public",
    (address) => {
      expect(isPrivateAddress(address)).toBe(false);
    },
  );
  it("rejects an approved domain whose DNS was rebound to an internal address", async () => {
    await expect(
      assertPublicProviderEndpoint("https://gateway.example/v1/chat", async () => [
        "169.254.169.254",
      ]),
    ).rejects.toThrow("内网");
  });
  it("rejects when any resolved record is internal (happy eyeballs dual-stack)", async () => {
    await expect(
      assertPublicProviderEndpoint("https://gateway.example/v1/chat", async () => [
        "93.184.216.34",
        "::1",
      ]),
    ).rejects.toThrow("内网");
  });
  it("accepts a domain resolving only to public addresses", async () => {
    await expect(
      assertPublicProviderEndpoint("https://gateway.example/v1/chat", async () => [
        "93.184.216.34",
      ]),
    ).resolves.toBe("https://gateway.example/v1/chat");
  });
  it("rejects failed or empty DNS resolution", async () => {
    await expect(
      assertPublicProviderEndpoint("https://gone.example/v1", async () => {
        throw new Error("ENOTFOUND");
      }),
    ).rejects.toThrow("无法解析");
    await expect(
      assertPublicProviderEndpoint("https://gone.example/v1", async () => []),
    ).rejects.toThrow("无法解析");
  });
  it("rejects literal internal IPs without needing lookup", async () => {
    await expect(
      assertPublicProviderEndpoint("https://127.0.0.1:8080/admin", async () => {
        throw new Error("lookup must not be called for literals");
      }),
    ).rejects.toThrow("内网");
  });
});

describe("upload and credential input validation", () => {
  it("rejects active content disguised using MIME or a docx suffix", () => {
    expect(() =>
      uploadType(Buffer.from("<html><script>alert(1)</script>"), "image/png"),
    ).toThrow();
    expect(() =>
      uploadType(
        Buffer.from("<html>"),
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
      ),
    ).toThrow();
    expect(
      uploadType(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), "image/png"),
    ).toBe(".png");
    expect(() =>
      uploadType(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), "image/jpeg"),
    ).toThrow();
  });
  it("bounds password types and encoded size", () => {
    expect(validPassword({ length: 32 })).toBe(false);
    expect(validPassword("a".repeat(1025))).toBe(false);
    expect(validPassword("密".repeat(400))).toBe(false);
    expect(validPassword("good-password")).toBe(true);
  });
});

describe("resource and production configuration boundaries", () => {
  it("rejects a tiny PDF with a huge MediaBox before allocating its canvas", async () => {
    const fs = await import("node:fs");
    const os = await import("node:os");
    const path = await import("node:path");
    const { documentImages } = await import("./document.mjs");
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mes-pdf-test-"));
    try {
      const objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 1000000 1000000] /Resources << >> >>",
      ];
      let pdf = "%PDF-1.4\n";
      const offsets = [0];
      objects.forEach((object, i) => {
        offsets.push(Buffer.byteLength(pdf));
        pdf += `${i + 1} 0 obj\n${object}\nendobj\n`;
      });
      const xref = Buffer.byteLength(pdf);
      pdf += `xref\n0 4\n0000000000 65535 f \n${offsets
        .slice(1)
        .map((offset) => String(offset).padStart(10, "0") + " 00000 n ")
        .join(
          "\n",
        )}\ntrailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF`;
      const file = path.join(dir, "huge.pdf");
      fs.writeFileSync(file, pdf);
      await expect(documentImages(file, "application/pdf")).rejects.toThrow(
        "PDF 页面尺寸过大",
      );
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
  it.each(["short", "请替换为至少32位随机字符串请替换为至少32位随机字符串"])(
    "refuses production with an unsafe APP_SECRET",
    async (secret) => {
      const { spawnSync } = await import("node:child_process");
      const result = spawnSync(process.execPath, ["server/index.mjs"], {
        cwd: new URL("..", import.meta.url),
        env: { ...process.env, NODE_ENV: "production", APP_SECRET: secret },
        encoding: "utf8",
        timeout: 5000,
      });
      expect(result.status).not.toBe(0);
      expect(result.stderr).toContain("生产环境必须配置");
    },
  );
});

it("rejects aggregate upload size before parsing file content and cleans temporary files", async () => {
  const { validateUploads } = await import("./security.mjs");
  const fs = await import("node:fs");
  const os = await import("node:os");
  const path = await import("node:path");
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mes-total-upload-"));
  const file = path.join(dir, "temporary");
  fs.writeFileSync(file, "fixture");
  let status = 0;
  const response = {
    once: () => {},
    status: (value: number) => {
      status = value;
      return response;
    },
    json: () => {},
  };
  try {
    validateUploads(
      { files: [{ path: file, size: 200 * 1024 * 1024 }] },
      response,
      () => {
        throw new Error("must reject");
      },
    );
    expect(status).toBe(413);
    expect(fs.existsSync(file)).toBe(false);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
