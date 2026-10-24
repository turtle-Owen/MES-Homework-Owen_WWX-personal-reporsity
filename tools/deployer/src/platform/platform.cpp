// 校园作业分析 · 桌面部署器 —— 平台无关的纯路径工具。
// 这里不出现任何 OS API，全部可在任何平台编译；OS 相关实现见
// platform_win.cpp / platform_posix.cpp。
#include "platform/platform.h"

#include <algorithm>
#include <cctype>

namespace mes::platform {
namespace {

bool isSeparator(char c) { return c == '/' || c == '\\'; }

bool isDriveLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Windows 盘符前缀，如 "C:" / "C:/" / "C:\"。
bool hasDrivePrefix(const std::string& path) {
  return path.size() >= 2 && isDriveLetter(path[0]) && path[1] == ':';
}

bool isAbsolutePath(const std::string& path) {
  if (path.empty()) return false;
  if (isSeparator(path[0])) return true;  // POSIX 根 / Windows 根或 UNC
  return hasDrivePrefix(path);
}

}  // namespace

std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  // b 为绝对路径时直接返回 b，避免 a + "/" + "/abs" 这类错误拼接。
  if (isAbsolutePath(b)) return b;
  const char last = a.back();
  if (isSeparator(last)) return a + b;
  return a + "/" + b;
}

std::string parentDir(const std::string& path) {
  if (path.empty()) return {};
  std::string p = path;
  // 去掉末尾分隔符（保留根："/"、"C:/"、"C:\"）。
  while (p.size() > 1 && isSeparator(p.back())) {
    if (p.size() == 3 && hasDrivePrefix(p)) break;
    p.pop_back();
  }
  if (p == "/" || p == "\\") return {};  // 根目录没有父目录
  const size_t pos = p.find_last_of("/\\");
  if (pos == std::string::npos) return {};
  // "C:/foo" -> "C:/"
  if (pos == 2 && hasDrivePrefix(p)) return p.substr(0, 3);
  // "/foo" -> "/"
  if (pos == 0) return p.substr(0, 1);
  return p.substr(0, pos);
}

std::string baseName(const std::string& path) {
  if (path.empty()) return {};
  std::string p = path;
  while (p.size() > 1 && isSeparator(p.back())) {
    if (p.size() == 3 && hasDrivePrefix(p)) break;
    p.pop_back();
  }
  const size_t pos = p.find_last_of("/\\");
  if (pos == std::string::npos) return p;
  if (pos + 1 >= p.size()) return p;  // 根目录本身
  return p.substr(pos + 1);
}

}  // namespace mes::platform
