// 校园作业分析 · 桌面部署器 —— ZIP 解压（miniz 3.x）
// 安全要点：
//   1. 拒绝绝对路径、盘符、上级目录（..）、Windows 会折叠的尾随空格/点；
//   2. 拒绝符号链接与 Windows 设备名（COM1/NUL/带 ADS 冒号等）；
//   3. 文件内容经 platform::writeFile 写出（二进制安全、Unicode 路径安全）；
//   4. POSIX 可执行位通过 platform::makeExecutable 保留。
#include "core/core.h"
#include "platform/platform.h"

#include "miniz.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mes::core {
namespace {

constexpr uint64_t kMaxEntryBytes = 1ULL << 30;    // 单文件 ≤ 1 GiB
constexpr uint64_t kMaxTotalBytes = 3ULL << 30;    // 解压总量 ≤ 3 GiB
constexpr size_t kMaxEntries = 200000;

bool isAlphaAscii(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

std::string lowerAscii(const std::string& text) {
  std::string out = text;
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return out;
}

// Windows 设备名（无论扩展名，CON.txt 同样被重定向到设备）。
bool isWindowsDeviceName(const std::string& segment) {
  std::string base = segment;
  const size_t dot = base.find('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  base = lowerAscii(base);
  // 去掉尾随空格后比较（WriteFile 会忽略这些空格）。
  while (!base.empty() && base.back() == ' ') base.pop_back();
  if (base == "con" || base == "prn" || base == "aux" || base == "nul") return true;
  if (base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0)) {
    const char digit = base[3];
    if (digit >= '1' && digit <= '9') return true;
  }
  return false;
}

// 校验并归一化 zip 条目路径；成功时 relativePath 不含前导分隔符、使用 '/'。
bool normalizeEntryPath(const std::string& rawName, std::string& relativePath,
                        std::string& error) {
  relativePath.clear();
  if (rawName.empty()) {
    error = "ZIP 中存在空文件名";
    return false;
  }
  if (rawName.size() > 1024) {
    error = "ZIP 条目路径过长：" + rawName.substr(0, 64) + "…";
    return false;
  }
  std::string name;
  name.reserve(rawName.size());
  for (char ch : rawName) {
    if (ch == '\\')
      name.push_back('/');
    else
      name.push_back(ch);
  }
  if (name.find('\0') != std::string::npos) {
    error = "ZIP 条目文件名包含非法字符（NUL）";
    return false;
  }
  if (name[0] == '/') {
    error = "ZIP 包含绝对路径条目，已拒绝：" + rawName;
    return false;
  }
  if (name.size() >= 2 && isAlphaAscii(name[0]) && name[1] == ':') {
    error = "ZIP 包含带盘符的绝对路径条目，已拒绝：" + rawName;
    return false;
  }
  std::vector<std::string> segments;
  std::string segment;
  for (char ch : name) {
    if (ch == '/') {
      if (segment.empty()) continue;  // 折叠 a//b
      segments.push_back(segment);
      segment.clear();
    } else {
      segment.push_back(ch);
    }
  }
  if (!segment.empty()) segments.push_back(segment);

  for (std::string& part : segments) {
    if (part == ".") {
      part.clear();
      continue;
    }
    if (part == "..") {
      error = "ZIP 包含越界路径（..），已拒绝：" + rawName;
      return false;
    }
    const unsigned char first = static_cast<unsigned char>(part.front());
    if (first < 0x20) {
      error = "ZIP 条目文件名包含控制字符，已拒绝：" + rawName;
      return false;
    }
    const std::string invalid = "<>:\"|?*";
    for (char ch : part) {
      if (invalid.find(ch) != std::string::npos) {
        error = "ZIP 条目文件名包含 Windows 非法字符：" + rawName;
        return false;
      }
    }
    // Windows 会静默去掉目录名尾随的空格与点，必须拒绝，否则可绕过 .. 检查。
    if (part.back() == ' ' || part.back() == '.') {
      error = "ZIP 条目文件名以空格或点结尾，已拒绝：" + rawName;
      return false;
    }
    if (isWindowsDeviceName(part)) {
      error = "ZIP 条目使用 Windows 保留设备名，已拒绝：" + rawName;
      return false;
    }
  }
  for (size_t i = 0; i < segments.size(); ++i) {
    if (segments[i].empty()) continue;
    if (!relativePath.empty()) relativePath.push_back('/');
    relativePath += segments[i];
  }
  return true;
}

bool isSymlinkEntry(const mz_zip_archive_file_stat& stat) {
  const uint32_t mode = static_cast<uint32_t>(stat.m_external_attr >> 16) & 0xFFFFu;
  const uint32_t fileType = mode & 0xF000u;
  return fileType == 0xA000u;  // S_IFLNK
}

bool wantsExecutableBit(const mz_zip_archive_file_stat& stat) {
  const uint32_t mode = static_cast<uint32_t>(stat.m_external_attr >> 16) & 0xFFFFu;
  return (mode & 0x49u) != 0;  // 0o111：任一执行位
}

std::string minizErrorText(mz_zip_error error) {
  switch (error) {
    case MZ_ZIP_FILE_OPEN_FAILED: return "无法打开 ZIP 文件";
    case MZ_ZIP_FILE_READ_FAILED: return "ZIP 文件读取失败（文件可能损坏或权限不足）";
    case MZ_ZIP_FILE_WRITE_FAILED: return "ZIP 文件写入失败";
    case MZ_ZIP_FILE_SEEK_FAILED: return "ZIP 文件定位失败";
    case MZ_ZIP_FILE_STAT_FAILED: return "ZIP 文件状态读取失败";
    case MZ_ZIP_ALLOC_FAILED: return "内存不足，无法解压安装包";
    case MZ_ZIP_NOT_AN_ARCHIVE: return "不是有效的 ZIP 文件（缺少签名）";
    case MZ_ZIP_ARCHIVE_TOO_LARGE: return "ZIP 文件结构异常或过大";
    case MZ_ZIP_INVALID_HEADER_OR_CORRUPTED: return "ZIP 头部损坏或不完整";
    case MZ_ZIP_INVALID_FILENAME: return "ZIP 内文件名无效";
    case MZ_ZIP_DECOMPRESSION_FAILED: return "ZIP 解压数据损坏";
    case MZ_ZIP_CRC_CHECK_FAILED: return "ZIP 校验失败（CRC 不匹配，文件已损坏）";
    case MZ_ZIP_UNSUPPORTED_METHOD: return "ZIP 使用了不支持的压缩算法";
    case MZ_ZIP_UNSUPPORTED_ENCRYPTION: return "ZIP 已加密，无法解压";
    default: return "ZIP 解压失败（miniz 错误码 " +
                    std::to_string(static_cast<int>(error)) + "）";
  }
}

}  // namespace

bool extractZip(const std::string& zipPath, const std::string& destDir,
                std::string& error) {
  error.clear();
  if (zipPath.empty() || destDir.empty()) {
    error = "ZIP 解压失败：路径参数为空";
    return false;
  }
  std::string archiveBytes;
  if (!mes::platform::readFile(zipPath, archiveBytes)) {
    error = "ZIP 解压失败：无法读取安装包 " + zipPath;
    return false;
  }
  if (archiveBytes.empty()) {
    error = "ZIP 解压失败：安装包为空（下载可能未完成）";
    return false;
  }
  mz_zip_archive archive;
  std::memset(&archive, 0, sizeof(archive));
  if (!mz_zip_reader_init_mem(&archive, archiveBytes.data(), archiveBytes.size(), 0)) {
    error = "ZIP 解压失败：" + minizErrorText(mz_zip_get_last_error(&archive));
    return false;
  }
  struct ArchiveGuard {
    mz_zip_archive* archive;
    ~ArchiveGuard() { mz_zip_reader_end(archive); }
  } guard{&archive};

  const mz_uint count = mz_zip_reader_get_num_files(&archive);
  if (count == 0) {
    error = "ZIP 解压失败：安装包内没有任何文件";
    return false;
  }
  if (count > kMaxEntries) {
    error = "ZIP 解压失败：安装包内文件数量异常（超过 " +
            std::to_string(kMaxEntries) + " 个）";
    return false;
  }

  if (!mes::platform::ensureDir(destDir)) {
    error = "ZIP 解压失败：无法创建目标目录 " + destDir;
    return false;
  }

  uint64_t totalBytes = 0;
  for (mz_uint index = 0; index < count; ++index) {
    mz_zip_archive_file_stat stat;
    std::memset(&stat, 0, sizeof(stat));
    if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
      error = "ZIP 解压失败：无法读取第 " + std::to_string(index + 1) +
              " 个条目的信息";
      return false;
    }
    std::string relative;
    if (!normalizeEntryPath(stat.m_filename, relative, error)) return false;
    if (relative.empty()) continue;  // 只有 "." 之类，忽略

    if (isSymlinkEntry(stat)) {
      error = "ZIP 包含符号链接条目，出于安全考虑已拒绝：" + std::string(stat.m_filename);
      return false;
    }

    const std::string target = mes::platform::joinPath(destDir, relative);
    const bool isDirectory =
        stat.m_is_directory != 0 || mz_zip_reader_is_file_a_directory(&archive, index) != 0;
    if (isDirectory) {
      if (!mes::platform::ensureDir(target)) {
        error = "ZIP 解压失败：无法创建目录 " + target;
        return false;
      }
      continue;
    }

    if (stat.m_uncomp_size > kMaxEntryBytes) {
      error = "ZIP 解压失败：文件过大 " + relative;
      return false;
    }
    totalBytes += stat.m_uncomp_size;
    if (totalBytes > kMaxTotalBytes) {
      error = "ZIP 解压失败：解压后总大小超过限制（3 GiB）";
      return false;
    }
    size_t extractedSize = 0;
    void* data = mz_zip_reader_extract_to_heap(&archive, index, &extractedSize, 0);
    if (!data && stat.m_uncomp_size != 0) {
      error = "ZIP 解压失败：无法解压 " + relative + "（" +
              minizErrorText(mz_zip_get_last_error(&archive)) + "）";
      return false;
    }
    std::string content;
    if (extractedSize > 0)
      content.assign(static_cast<const char*>(data), extractedSize);
    if (data) mz_free(data);
    if (!mes::platform::writeFile(target, content)) {
      error = "ZIP 解压失败：无法写入文件 " + target;
      return false;
    }
    if (wantsExecutableBit(stat)) mes::platform::makeExecutable(target);
  }
  return true;
}

}  // namespace mes::core
