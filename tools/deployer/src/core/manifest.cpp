// 校园作业分析 · 桌面部署器 —— Release 清单解析与资产解析
#include "core/core.h"
#include "core/json.h"
#include "core/manifest_parse.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mes::core {
namespace {

std::string trimCopy(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end &&
         (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
          text[begin] == '\n'))
    ++begin;
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
          text[end - 1] == '\n'))
    --end;
  return text.substr(begin, end - begin);
}

// 清单里的 size 是 JSON number（double 精度足以覆盖几十亿字节）。
uint64_t numberToSize(double value) {
  if (!(value > 0.0)) return 0;
  if (value > 9.0e18) return 0;
  return static_cast<uint64_t>(value);
}

bool readRequiredString(const sc::Json& object, const std::string& key,
                        const std::string& context, std::string& out,
                        std::string& error) {
  const sc::Json& node = object[key];
  if (!node.isString()) {
    error = context + "缺少字符串字段 " + key;
    return false;
  }
  out = trimCopy(node.asString());
  if (out.empty()) {
    error = context + "字段 " + key + " 不能为空";
    return false;
  }
  return true;
}

}  // namespace

bool isValidSha256(const std::string& value) {
  if (value.empty()) return true;  // 未提供
  if (value.size() != 64) return false;
  for (char ch : value) {
    const bool digit = ch >= '0' && ch <= '9';
    const bool lower = ch >= 'a' && ch <= 'f';
    const bool upper = ch >= 'A' && ch <= 'F';
    if (!digit && !lower && !upper) return false;
  }
  return true;
}

bool parseManifest(const std::string& json, Manifest& out, std::string& error) {
  out = Manifest{};
  error.clear();
  if (trimCopy(json).empty()) {
    error = "安装清单内容为空（可能是下载失败或网络被拦截）";
    return false;
  }
  sc::Json root;
  std::string parseError;
  if (!sc::Json::parse(json, root, &parseError)) {
    error = "安装清单 JSON 解析失败：" + parseError;
    return false;
  }
  if (!root.isObject()) {
    error = "安装清单格式不正确：顶层必须是 JSON 对象";
    return false;
  }
  if (!readRequiredString(root, "version", "安装清单", out.version, error))
    return false;
  const sc::Json& createdAt = root["createdAt"];
  if (createdAt.isString()) out.createdAt = trimCopy(createdAt.asString());
  // 兼容 CI 里常见的 "created_at" 写法。
  if (out.createdAt.empty()) {
    const sc::Json& alternate = root["created_at"];
    if (alternate.isString()) out.createdAt = trimCopy(alternate.asString());
  }

  const sc::Json& entries = root["entries"];
  if (!entries.isArray() || entries.size() == 0) {
    error = "安装清单至少需要包含一个 entries 条目";
    return false;
  }
  for (size_t index = 0; index < entries.size(); ++index) {
    const sc::Json& item = entries.at(index);
    const std::string context =
        "安装清单第 " + std::to_string(index + 1) + " 个条目";
    if (!item.isObject()) {
      error = context + "不是 JSON 对象";
      return false;
    }
    ManifestEntry entry;
    if (!readRequiredString(item, "platform", context, entry.platform, error))
      return false;
    if (!readRequiredString(item, "arch", context, entry.arch, error))
      return false;
    if (!readRequiredString(item, "asset", context, entry.asset, error))
      return false;
    entry.platform = trimCopy(entry.platform);
    entry.arch = trimCopy(entry.arch);
    // 资产名只允许文件名，不允许携带路径或 ..，避免被清单注入越界路径。
    if (entry.asset.find('/') != std::string::npos ||
        entry.asset.find('\\') != std::string::npos ||
        entry.asset.find("..") != std::string::npos) {
      error = context + "的 asset 必须是合法文件名：" + entry.asset;
      return false;
    }
    const sc::Json& sha = item["sha256"];
    if (sha.isString()) entry.sha256 = trimCopy(sha.asString());
    if (!isValidSha256(entry.sha256)) {
      error = context + "的 sha256 不是 64 位十六进制字符串";
      return false;
    }
    const sc::Json& size = item["size"];
    if (size.isNumber()) entry.size = numberToSize(size.asNumber());
    const sc::Json& nodeVersion = item["nodeVersion"];
    if (nodeVersion.isString()) entry.nodeVersion = trimCopy(nodeVersion.asString());
    if (entry.nodeVersion.empty()) {
      const sc::Json& alternate = item["node_version"];
      if (alternate.isString()) entry.nodeVersion = trimCopy(alternate.asString());
    }
    out.entries.push_back(entry);
  }
  if (out.version.empty() || out.entries.empty()) {
    error = "安装清单内容不完整";
    return false;
  }
  return true;
}

const ManifestEntry* findManifestEntry(const Manifest& manifest,
                                       const PlatformInfo& platform) {
  for (const ManifestEntry& entry : manifest.entries) {
    if (entry.platform == platform.os && entry.arch == platform.arch)
      return &entry;
  }
  return nullptr;
}

std::string manifestEntryUrl(const std::string& tag, const ManifestEntry& entry) {
  return releaseBaseUrl(tag) + entry.asset;
}

std::string manifestAssetName() { return "mes-homework-app-manifest.json"; }

std::string appAssetName(const PlatformInfo& platform) {
  return "mes-homework-app-" + platform.os + "-" + platform.arch + ".zip";
}

std::string releaseBaseUrl(const std::string& tag) {
  return "https://github.com/" + std::string(kRepoSlug) + "/releases/download/" +
         tag + "/";
}

std::string payloadUrlFor(const std::string& tag, const PlatformInfo& platform) {
  return releaseBaseUrl(tag) + appAssetName(platform);
}

std::string deployerAssetName(const PlatformInfo& platform) {
  return "mes-deployer-" + platform.os + "-" + platform.arch +
         (platform.os == "windows" ? ".exe" : "");
}

}  // namespace mes::core
