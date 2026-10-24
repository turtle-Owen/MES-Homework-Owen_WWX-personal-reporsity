// 校园作业分析 · 桌面部署器 —— Release 清单解析（core 内部接口）
// 该头文件仅供 core/ 内部与单元测试使用，不属于对外契约（core.h）。
#pragma once

#include "core/core.h"

#include <string>

namespace mes::core {

// 解析 GitHub Release 中的 mes-homework-app-manifest.json。
// 只读取 version / createdAt / entries[{platform,arch,asset,sha256,size,nodeVersion}]，
// 其余字段（例如 CI 附带的 build）直接忽略。
// 失败时返回 false，并在 error 中给出中文原因。
bool parseManifest(const std::string& json, Manifest& out, std::string& error);

// 在清单中查找与当前平台完全匹配的条目（platform + arch）。
// 找不到返回 nullptr（调用方负责给出中文错误）。
const ManifestEntry* findManifestEntry(const Manifest& manifest,
                                       const PlatformInfo& platform);

// 清单条目对应的下载地址（releaseBaseUrl + asset 文件名）。
std::string manifestEntryUrl(const std::string& tag, const ManifestEntry& entry);

// 校验 sha256 是否为 64 位十六进制字符串（空串视为“未提供”，返回 true）。
bool isValidSha256(const std::string& value);

}  // namespace mes::core
