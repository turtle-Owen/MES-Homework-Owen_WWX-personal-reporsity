// 校园作业分析 · 桌面部署器 —— Windows 平台能力实现（Win32，UTF-16 API + UTF-8 存储）。
//
// 本文件同时被 mes-deployer 与 mes-deployer-test 编译，因此**不能**引用 GLFW /
// DWM / WinHTTP 等只属于主目标的符号：窗口增强在 window_win.cpp，下载在
// http_win.cpp。
#include "platform/platform.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace mes::platform {
namespace {

// ---------------------------------------------------------------- UTF 转换
std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      wide.data(), needed);
  return wide;
}

std::string wideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string utf8(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      utf8.data(), needed, nullptr, nullptr);
  return utf8;
}

// ---------------------------------------------------------------- 路径工具
bool isSepW(wchar_t c) { return c == L'/' || c == L'\\'; }

std::wstring trimTrailingSepW(std::wstring path) {
  while (path.size() > 1 && isSepW(path.back())) {
    if (path.size() == 3 && path[1] == L':') break;  // "C:\" 保留
    path.pop_back();
  }
  return path;
}

std::wstring parentW(const std::wstring& path) {
  if (path.empty()) return {};
  std::wstring p = trimTrailingSepW(path);
  const size_t pos = p.find_last_of(L"/\\");
  if (pos == std::wstring::npos) return {};
  if (pos == 2 && p.size() >= 2 && p[1] == L':') return p.substr(0, 3);
  if (pos == 0) return p.substr(0, 1);
  return p.substr(0, pos);
}

std::wstring joinW(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (!a.empty() && isSepW(a.back())) return a + b;
  return a + L"\\" + b;
}

std::string trimTrailingSepsUtf8(std::string path) {
  while (path.size() > 1 && isSepW(path.back())) {
    if (path.size() == 3 && path[1] == ':') break;
    path.pop_back();
  }
  return path;
}

std::wstring fullPathW(const std::wstring& path) {
  if (path.empty()) return {};
  DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (needed == 0) return path;
  std::wstring out(static_cast<size_t>(needed), L'\0');
  const DWORD written =
      GetFullPathNameW(path.c_str(), needed, out.data(), nullptr);
  if (written == 0 || written >= needed) return path;
  out.resize(written);
  return out;
}

std::wstring lowerW(std::wstring text) {
  for (wchar_t& c : text) {
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
  }
  return text;
}

// ---------------------------------------------------------------- 错误文本
std::string win32ErrorText(DWORD code) {
  LPWSTR buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::string text;
  if (length > 0 && buffer) {
    text = wideToUtf8(std::wstring(buffer, length));
    LocalFree(buffer);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                             text.back() == ' ' || text.back() == '.'))
      text.pop_back();
  }
  if (text.empty()) text = "错误码 " + std::to_string(static_cast<unsigned>(code));
  return text;
}

// ---------------------------------------------------------------- 文件系统
bool fileExistsW(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) return false;
  return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool dirExistsW(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) return false;
  return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ensureDirW(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES)
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const std::wstring parent = parentW(path);
  if (!parent.empty() && parent != path) {
    if (!ensureDirW(parent)) return false;
  }
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  if (GetLastError() == ERROR_ALREADY_EXISTS) return dirExistsW(path);
  return false;
}

bool removeTreeW(const std::wstring& path) {
  if (path.empty()) return false;
  const DWORD attr = GetFileAttributesW(path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return GetLastError() == ERROR_FILE_NOT_FOUND ||
           GetLastError() == ERROR_PATH_NOT_FOUND;
  }
  // 重解析点（符号链接 / junction）：只删除链接本身，绝不进入目标目录。
  if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
      return RemoveDirectoryW(path.c_str()) != 0;
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(path.c_str()) != 0;
  }
  if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(path.c_str()) != 0;
  }
  std::wstring pattern = path;
  if (!pattern.empty() && !isSepW(pattern.back())) pattern += L'\\';
  pattern += L'*';
  WIN32_FIND_DATAW entry{};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &entry);
  if (handle != INVALID_HANDLE_VALUE) {
    bool ok = true;
    do {
      if (wcscmp(entry.cFileName, L".") == 0 ||
          wcscmp(entry.cFileName, L"..") == 0)
        continue;
      if (!removeTreeW(joinW(path, entry.cFileName))) ok = false;
    } while (FindNextFileW(handle, &entry));
    FindClose(handle);
    if (!ok) return false;
  }
  if (RemoveDirectoryW(path.c_str())) return true;
  return GetLastError() == ERROR_FILE_NOT_FOUND ||
         GetLastError() == ERROR_PATH_NOT_FOUND;
}

bool writeFileW(const std::wstring& path, const std::string& content) {
  if (path.empty()) return false;
  const std::wstring parent = parentW(path);
  if (!parent.empty() && !ensureDirW(parent)) return false;
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  bool ok = true;
  size_t offset = 0;
  while (offset < content.size()) {
    const DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(content.size() - offset, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(handle, content.data() + offset, chunk, &written, nullptr) ||
        written != chunk) {
      ok = false;
      break;
    }
    offset += written;
  }
  CloseHandle(handle);
  return ok;
}

bool readFileW(const std::wstring& path, std::string& out) {
  out.clear();
  if (path.empty()) return false;
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (GetFileSizeEx(handle, &size) && size.QuadPart > 0)
    out.reserve(static_cast<size_t>(std::min<LONGLONG>(size.QuadPart, 64LL << 20)));
  bool ok = true;
  char buffer[65536];
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(handle, buffer, sizeof(buffer), &got, nullptr)) {
      ok = false;
      break;
    }
    if (got == 0) break;
    out.append(buffer, got);
  }
  CloseHandle(handle);
  if (!ok) out.clear();
  return ok;
}

uint64_t fileSizeW(const std::wstring& path) {
  if (path.empty()) return 0;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
  return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
         static_cast<uint64_t>(data.nFileSizeLow);
}

bool copyFileW(const std::wstring& from, const std::wstring& to,
               std::string& error) {
  const std::wstring parent = parentW(to);
  if (!parent.empty() && !ensureDirW(parent)) {
    error = "复制失败：无法创建目标目录 " + wideToUtf8(parent);
    return false;
  }
  if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
    error = "复制文件失败：" + wideToUtf8(from) + "（" +
            win32ErrorText(GetLastError()) + "）";
    return false;
  }
  return true;
}

bool copyTreeW(const std::wstring& from, const std::wstring& to,
               std::string& error) {
  const std::wstring absFrom = trimTrailingSepW(fullPathW(from));
  const std::wstring absTo = trimTrailingSepW(fullPathW(to));
  if (!dirExistsW(absFrom)) {
    error = "复制失败：源目录不存在 " + wideToUtf8(from);
    return false;
  }
  const std::wstring lowerFrom = lowerW(absFrom);
  const std::wstring lowerTo = lowerW(absTo);
  if (lowerFrom == lowerTo) {
    error = "复制失败：源目录与目标目录相同";
    return false;
  }
  if (lowerTo.size() > lowerFrom.size() &&
      lowerTo.compare(0, lowerFrom.size(), lowerFrom) == 0 &&
      isSepW(lowerTo[lowerFrom.size()])) {
    error = "复制失败：目标目录位于源目录内部（会造成递归复制）";
    return false;
  }
  if (!ensureDirW(absTo)) {
    error = "复制失败：无法创建目标目录 " + wideToUtf8(to);
    return false;
  }
  std::wstring pattern = absFrom;
  if (!pattern.empty() && !isSepW(pattern.back())) pattern += L'\\';
  pattern += L'*';
  WIN32_FIND_DATAW entry{};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &entry);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND) return true;  // 空目录
    error = "复制失败：无法读取源目录（" + win32ErrorText(code) + "）";
    return false;
  }
  bool ok = true;
  do {
    if (wcscmp(entry.cFileName, L".") == 0 ||
        wcscmp(entry.cFileName, L"..") == 0)
      continue;
    const std::wstring src = joinW(absFrom, entry.cFileName);
    const std::wstring dst = joinW(absTo, entry.cFileName);
    if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        // 不跟随目录链接，避免越界与递归环。
        ensureDirW(dst);
        continue;
      }
      if (!copyTreeW(src, dst, error)) {
        ok = false;
        break;
      }
    } else {
      if (!copyFileW(src, dst, error)) {
        ok = false;
        break;
      }
    }
  } while (FindNextFileW(handle, &entry));
  FindClose(handle);
  return ok;
}

// ---------------------------------------------------------------- 环境变量
std::wstring envVarW(const std::wstring& name) {
  if (name.empty()) return {};
  DWORD needed = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
  if (needed == 0) return {};
  std::wstring value(static_cast<size_t>(needed), L'\0');
  const DWORD written =
      GetEnvironmentVariableW(name.c_str(), value.data(), needed);
  if (written == 0 || written >= needed) return {};
  value.resize(written);
  return value;
}

std::wstring envVarWUtf8(const std::string& name) {
  return envVarW(utf8ToWide(name));
}

// 在继承父环境的基础上应用覆盖项，生成 UTF-16 环境块（双 NUL 结尾）。
bool buildEnvBlock(const std::vector<EnvPair>& env,
                   std::vector<wchar_t>& block) {
  std::vector<std::wstring> entries;
  LPWCH inherited = GetEnvironmentStringsW();
  if (inherited) {
    for (LPWCH p = inherited; *p != L'\0'; p += wcslen(p) + 1)
      entries.emplace_back(p);
    FreeEnvironmentStringsW(inherited);
  }
  auto keyOf = [](const std::wstring& entry) -> std::wstring {
    const size_t pos = entry.find(L'=');
    if (pos == std::wstring::npos) return entry;
    if (pos == 0) {  // 特殊变量（如 "=C:=C:\path"）
      const size_t second = entry.find(L'=', 1);
      return second == std::wstring::npos ? entry : entry.substr(0, second);
    }
    return entry.substr(0, pos);
  };
  for (const auto& pair : env) {
    if (pair.first.empty()) continue;
    const std::wstring key = utf8ToWide(pair.first);
    const std::wstring value = utf8ToWide(pair.second);
    const std::wstring entry = key + L"=" + value;
    const std::wstring lowerKey = lowerW(key);
    bool replaced = false;
    for (std::wstring& existing : entries) {
      if (lowerW(keyOf(existing)) == lowerKey) {
        existing = entry;
        replaced = true;
        break;
      }
    }
    if (!replaced) entries.push_back(entry);
  }
  // 按 Windows 惯例排序（不区分大小写），提高环境块兼容性。
  std::sort(entries.begin(), entries.end(),
            [](const std::wstring& a, const std::wstring& b) {
              return lowerW(a) < lowerW(b);
            });
  block.clear();
  for (const std::wstring& entry : entries) {
    block.insert(block.end(), entry.begin(), entry.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return true;
}

// ---------------------------------------------------------------- 命令行
// 按 Windows CommandLineToArgvW / MSVC CRT 的规则给参数加引号。
std::wstring quoteWinArg(const std::wstring& arg) {
  if (arg.empty()) return L"\"\"";
  bool needQuote = false;
  for (wchar_t c : arg) {
    if (c == L' ' || c == L'\t' || c == L'"') {
      needQuote = true;
      break;
    }
  }
  if (!needQuote) return arg;
  std::wstring out = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

std::wstring buildCommandLine(const std::vector<std::string>& args) {
  std::wstring line;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) line.push_back(L' ');
    line += quoteWinArg(utf8ToWide(args[i]));
  }
  return line;
}

// ---------------------------------------------------------------- 动态函数
template <typename Fn>
Fn loadFunction(HMODULE module, const char* name) {
  if (!module) return nullptr;
  const FARPROC address = GetProcAddress(module, name);
  if (!address) return nullptr;
  return reinterpret_cast<Fn>(reinterpret_cast<void*>(address));
}

// ---------------------------------------------------------------- 随机数
bool fillRandomBytes(unsigned char* buffer, size_t size) {
  if (size == 0) return true;
  // Vista+：bcrypt BCryptGenRandom（动态加载，避免引入 bcrypt 链接依赖）
  using BCryptGenRandomFn = LONG(WINAPI*)(void*, unsigned char*, ULONG, ULONG);
  static BCryptGenRandomFn bcryptGen = []() -> BCryptGenRandomFn {
    return loadFunction<BCryptGenRandomFn>(LoadLibraryW(L"bcrypt.dll"),
                                           "BCryptGenRandom");
  }();
  if (bcryptGen) {
    constexpr ULONG kUseSystemPreferredRng = 0x00000002UL;
    if (bcryptGen(nullptr, buffer, static_cast<ULONG>(size),
                  kUseSystemPreferredRng) == 0)
      return true;
  }
  // 回退：advapi32!SystemFunction036（RtlGenRandom）
  using RtlGenRandomFn = BOOLEAN(WINAPI*)(PVOID, ULONG);
  static RtlGenRandomFn rtlGen = []() -> RtlGenRandomFn {
    return loadFunction<RtlGenRandomFn>(GetModuleHandleW(L"advapi32.dll"),
                                        "SystemFunction036");
  }();
  if (rtlGen) {
    if (rtlGen(buffer, static_cast<ULONG>(size))) return true;
  }
  // 最后回退：std::random_device（尽最大努力）
  try {
    std::random_device device;
    for (size_t i = 0; i < size; ++i)
      buffer[i] = static_cast<unsigned char>(device() & 0xFFu);
    return true;
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------- 进程类
class WinProcess final : public Process {
 public:
  WinProcess(HANDLE process, HANDLE job, DWORD processId)
      : process_(process), job_(job), pid_(processId) {}

  ~WinProcess() override {
    if (job_) CloseHandle(job_);
    if (process_) CloseHandle(process_);
  }

  bool running() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (exited_) return false;
    if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
    finalizeLocked();
    return false;
  }

  int exitCode() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exited_) {
      if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return -1;
      finalizeLocked();
    }
    return static_cast<int>(exitCode_);
  }

  void stop() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (exited_) return;
    if (job_) {
      TerminateJobObject(job_, 1);  // 连同子进程树一起结束
    } else {
      TerminateProcess(process_, 1);
    }
    WaitForSingleObject(process_, 3000);
    if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) finalizeLocked();
  }

  uint64_t pid() const override { return static_cast<uint64_t>(pid_); }

 private:
  void finalizeLocked() const {
    DWORD code = 0;
    if (GetExitCodeProcess(process_, &code)) exitCode_ = code;
    exited_ = true;
  }

  mutable std::mutex mutex_;
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
  DWORD pid_ = 0;
  mutable DWORD exitCode_ = 0;
  mutable bool exited_ = false;
};

// taskkill /PID <pid> /T /F：用于没有 Job 对象的 detach 进程树。
bool taskkillTree(DWORD pid) {
  if (pid == 0) return false;
  std::wstring command = L"taskkill.exe /PID " + std::to_wstring(pid) +
                         L" /T /F";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> mutableCommand(command.begin(), command.end());
  mutableCommand.push_back(L'\0');
  if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return false;
  CloseHandle(pi.hThread);
  const DWORD wait = WaitForSingleObject(pi.hProcess, 10000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return wait != WAIT_TIMEOUT && code == 0;
}

// 创建用于继承的日志文件句柄（追加写，允许子进程读句柄）。
HANDLE openLogHandle(const std::wstring& logFile) {
  if (logFile.empty()) return INVALID_HANDLE_VALUE;
  ensureDirW(parentW(logFile));
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  return CreateFileW(logFile.c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

}  // namespace

// ================================================================ 基础信息
std::string platformName() { return "windows"; }

std::string architecture() {
#if defined(_M_X64) || defined(__x86_64__)
  return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#else
  return "unknown";
#endif
}

std::string osVersionLabel() {
  struct OsVersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platformId;
    wchar_t csdVersion[128];
  } info{};
  info.size = sizeof(info);
  using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
  RtlGetVersionFn rtlGetVersion = nullptr;
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
    rtlGetVersion =
        loadFunction<RtlGetVersionFn>(ntdll, "RtlGetVersion");
  bool got = false;
  if (rtlGetVersion && rtlGetVersion(&info) == 0) {
    got = true;
  } else {
    OSVERSIONINFOW legacy{};
    legacy.dwOSVersionInfoSize = sizeof(legacy);
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    if (GetVersionExW(&legacy)) {
      info.major = legacy.dwMajorVersion;
      info.minor = legacy.dwMinorVersion;
      info.build = legacy.dwBuildNumber;
      got = true;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  }
  if (!got) return "Windows (版本未知)";
  std::string name;
  if (info.major == 10) {
    name = info.build >= 22000 ? "Windows 11" : "Windows 10";
  } else if (info.major == 6 && info.minor == 3) {
    name = "Windows 8.1";
  } else if (info.major == 6 && info.minor == 2) {
    name = "Windows 8";
  } else if (info.major == 6 && info.minor == 1) {
    name = "Windows 7";
  } else if (info.major == 6 && info.minor == 0) {
    name = "Windows Vista";
  } else {
    name = "Windows " + std::to_string(info.major) + "." +
           std::to_string(info.minor);
  }
  return name + " (build " + std::to_string(info.build) + ")";
}

std::string executablePath() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (int attempt = 0; attempt < 8; ++attempt) {
    const DWORD written =
        GetModuleFileNameW(nullptr, buffer.data(),
                           static_cast<DWORD>(buffer.size()));
    if (written == 0) return {};
    if (written < buffer.size()) {
      buffer.resize(written);
      return wideToUtf8(buffer);
    }
    buffer.resize(buffer.size() * 2);
  }
  return {};
}

std::string currentDir() {
  DWORD needed = GetCurrentDirectoryW(0, nullptr);
  if (needed == 0) return {};
  std::wstring buffer(static_cast<size_t>(needed), L'\0');
  const DWORD written = GetCurrentDirectoryW(needed, buffer.data());
  if (written == 0 || written >= needed) return {};
  buffer.resize(written);
  return wideToUtf8(buffer);
}

std::string homeDir() {
  std::wstring home = envVarW(L"USERPROFILE");
  if (home.empty()) {
    const std::wstring drive = envVarW(L"HOMEDRIVE");
    const std::wstring path = envVarW(L"HOMEPATH");
    home = drive + path;
  }
  if (!home.empty()) return trimTrailingSepsUtf8(wideToUtf8(home));
  return currentDir();
}

std::string tempDir() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (int attempt = 0; attempt < 4; ++attempt) {
    const DWORD written =
        GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (written == 0) break;
    if (written < buffer.size()) {
      buffer.resize(written);
      return trimTrailingSepsUtf8(wideToUtf8(buffer));
    }
    buffer.resize(buffer.size() * 2);
  }
  std::string fallback = wideToUtf8(envVarW(L"TEMP"));
  if (fallback.empty()) fallback = wideToUtf8(envVarW(L"TMP"));
  if (fallback.empty()) fallback = currentDir();
  return fallback;
}

// ================================================================ 文件系统
bool ensureDir(const std::string& path) {
  if (path.empty()) return false;
  return ensureDirW(utf8ToWide(path));
}

bool removeTree(const std::string& path) {
  if (path.empty()) return false;
  return removeTreeW(utf8ToWide(path));
}

bool fileExists(const std::string& path) {
  if (path.empty()) return false;
  return fileExistsW(utf8ToWide(path));
}

bool dirExists(const std::string& path) {
  if (path.empty()) return false;
  return dirExistsW(utf8ToWide(path));
}

bool writeFile(const std::string& path, const std::string& content) {
  if (path.empty()) return false;
  return writeFileW(utf8ToWide(path), content);
}

bool readFile(const std::string& path, std::string& out) {
  if (path.empty()) {
    out.clear();
    return false;
  }
  return readFileW(utf8ToWide(path), out);
}

uint64_t fileSize(const std::string& path) {
  if (path.empty()) return 0;
  return fileSizeW(utf8ToWide(path));
}

bool copyTree(const std::string& from, const std::string& to,
              std::string& error) {
  error.clear();
  if (from.empty() || to.empty()) {
    error = "复制失败：源目录或目标目录为空";
    return false;
  }
  if (!copyTreeW(utf8ToWide(from), utf8ToWide(to), error)) return false;
  return true;
}

bool makeExecutable(const std::string& /*path*/) {
  // Windows 上没有 POSIX 执行位概念，保留 .exe 扩展名可执行即可。
  return true;
}

std::string randomHex(size_t bytes) {
  if (bytes == 0) return {};
  if (bytes > 4096) bytes = 4096;
  std::vector<unsigned char> buffer(bytes);
  if (!fillRandomBytes(buffer.data(), buffer.size())) return {};
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  for (unsigned char byte : buffer) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0F]);
  }
  return out;
}

std::string envVar(const std::string& name) {
  if (name.empty()) return {};
  return wideToUtf8(envVarWUtf8(name));
}

bool isRunningAsAdmin() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD length = 0;
  bool admin = false;
  if (GetTokenInformation(token, TokenElevation, &elevation,
                         sizeof(elevation), &length)) {
    admin = elevation.TokenIsElevated != 0;
  }
  CloseHandle(token);
  return admin;
}

bool killProcessTree(uint64_t pid) {
  if (pid == 0) return false;
  const DWORD target = static_cast<DWORD>(pid);
  if (taskkillTree(target)) return true;
  // 回退：直接终止目标进程（拿不到子进程树）。
  HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, target);
  if (!handle) return false;
  TerminateProcess(handle, 1);
  WaitForSingleObject(handle, 3000);
  CloseHandle(handle);
  return true;
}

// ================================================================ 字体
std::vector<FontCandidate> fontCandidates() {
  std::vector<std::wstring> dirs;
  wchar_t windowsDir[MAX_PATH] = {0};
  if (GetWindowsDirectoryW(windowsDir, MAX_PATH) > 0)
    dirs.push_back(std::wstring(windowsDir) + L"\\Fonts");
  const std::wstring localAppData = envVarW(L"LOCALAPPDATA");
  if (!localAppData.empty())
    dirs.push_back(localAppData + L"\\Microsoft\\Windows\\Fonts");

  // 顺序即优先级：优先中文（CJK）字体。
  static const wchar_t* kNames[] = {
      L"msyh.ttc",    L"msyhbd.ttc",  L"msyhl.ttc",  L"simhei.ttf",
      L"simsun.ttc",  L"simsunb.ttf", L"Deng.ttf",   L"msjhl.ttc",
      L"segoeui.ttf", L"segoeuib.ttf", L"arial.ttf",
  };
  std::vector<FontCandidate> candidates;
  std::vector<std::wstring> seen;
  for (const wchar_t* name : kNames) {
    for (const std::wstring& dir : dirs) {
      const std::wstring path = dir + L"\\" + name;
      if (!fileExistsW(path)) continue;
      const std::string utf8 = wideToUtf8(path);
      bool duplicate = false;
      for (const std::wstring& existing : seen) {
        if (lowerW(existing) == lowerW(path)) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      seen.push_back(path);
      candidates.push_back(FontCandidate{utf8, 0});
    }
  }
  if (candidates.empty()) {
    // 极端情况下没有找到任何字体文件：给出约定路径，让 UI 尝试加载后回退默认字体。
    candidates.push_back(FontCandidate{"C:/Windows/Fonts/msyh.ttc", 0});
    candidates.push_back(FontCandidate{"C:/Windows/Fonts/segoeui.ttf", 0});
  }
  return candidates;
}

std::string preferredUiFont() {
  const std::vector<FontCandidate> candidates = fontCandidates();
  if (candidates.empty()) return {};
  // fontCandidates() 已按 CJK 优先排序：msyh/simhei/simsun/Deng 在前。
  for (const FontCandidate& candidate : candidates) {
    const std::wstring lower = lowerW(utf8ToWide(candidate.path));
    if (lower.find(L"msyh") != std::wstring::npos ||
        lower.find(L"simhei") != std::wstring::npos ||
        lower.find(L"simsun") != std::wstring::npos ||
        lower.find(L"deng") != std::wstring::npos ||
        lower.find(L"msjhl") != std::wstring::npos) {
      return candidate.path;
    }
  }
  return candidates.front().path;
}

// ================================================================ 进程
std::unique_ptr<Process> spawnProcess(const std::vector<std::string>& args,
                                      const std::string& workDir,
                                      const std::vector<EnvPair>& env,
                                      const std::string& logFile,
                                      std::string& error,
                                      bool detached) {
  error.clear();
  if (args.empty() || args[0].empty()) {
    error = "启动失败：命令为空";
    return nullptr;
  }
  std::wstring commandLine = buildCommandLine(args);
  std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
  commandBuffer.push_back(L'\0');
  const std::wstring workDirW = utf8ToWide(workDir);

  std::vector<wchar_t> envBlock;
  buildEnvBlock(env, envBlock);
  if (envBlock.empty()) envBlock.push_back(L'\0');

  HANDLE logHandle = openLogHandle(utf8ToWide(logFile));
  if (!logFile.empty() && logHandle == INVALID_HANDLE_VALUE) {
    error = "启动失败：无法打开日志文件 " + logFile;
    return nullptr;
  }

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  bool inheritHandles = false;
  std::vector<unsigned char> attributeStorage;
  LPPROC_THREAD_ATTRIBUTE_LIST attributeList = nullptr;
  DWORD flags = CREATE_UNICODE_ENVIRONMENT;
  if (logHandle != INVALID_HANDLE_VALUE) {
    STARTUPINFOW* si = &startup.StartupInfo;
    si->dwFlags |= STARTF_USESTDHANDLES;
    si->hStdOutput = logHandle;
    si->hStdError = logHandle;
    si->hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    inheritHandles = true;
    // 只把日志句柄传给子进程，避免泄漏其它可继承句柄。
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    if (attributeSize > 0) {
      attributeStorage.resize(attributeSize);
      attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
          attributeStorage.data());
      if (InitializeProcThreadAttributeList(attributeList, 1, 0,
                                            &attributeSize)) {
        if (!UpdateProcThreadAttribute(
                attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                &logHandle, sizeof(logHandle), nullptr, nullptr)) {
          DeleteProcThreadAttributeList(attributeList);
          attributeList = nullptr;
        } else {
          startup.StartupInfo.cb = sizeof(startup);
          flags |= EXTENDED_STARTUPINFO_PRESENT;
        }
      } else {
        attributeList = nullptr;
      }
    }
  }
  if (detached) {
    flags |= DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
  } else {
    flags |= CREATE_NO_WINDOW | CREATE_SUSPENDED;
  }

  PROCESS_INFORMATION processInfo{};
  const BOOL created = CreateProcessW(
      nullptr, commandBuffer.data(), nullptr, nullptr,
      inheritHandles ? TRUE : FALSE, flags,
      envBlock.empty() ? nullptr : static_cast<LPVOID>(envBlock.data()),
      workDirW.empty() ? nullptr : workDirW.c_str(), &startup.StartupInfo,
      &processInfo);
  if (attributeList) DeleteProcThreadAttributeList(attributeList);
  if (logHandle != INVALID_HANDLE_VALUE) CloseHandle(logHandle);
  if (!created) {
    error = "启动失败：" + args[0] + "（" + win32ErrorText(GetLastError()) + "）";
    return nullptr;
  }
  HANDLE job = nullptr;
  if (!detached) {
    // 非 detached 进程放进 Job 对象：stop() 可连同子进程树一起终止；
    // 部署器退出时句柄关闭也会带走残留子进程。
    job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags =
          JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                   &limits, sizeof(limits)) ||
          !AssignProcessToJobObject(job, processInfo.hProcess)) {
        CloseHandle(job);
        job = nullptr;
      }
    }
    ResumeThread(processInfo.hThread);  // 恢复 CREATE_SUSPENDED 启动的子进程
  }
  CloseHandle(processInfo.hThread);
  return std::unique_ptr<Process>(new WinProcess(processInfo.hProcess, job,
                                                 processInfo.dwProcessId));
}

bool runCommand(const std::vector<std::string>& args,
                const std::string& workDir, std::string& output,
                std::string& error, int timeoutMs) {
  output.clear();
  error.clear();
  if (args.empty() || args[0].empty()) {
    error = "命令为空";
    return false;
  }
  if (timeoutMs <= 0) timeoutMs = 30000;

  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE readPipe = INVALID_HANDLE_VALUE;
  HANDLE writePipe = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&readPipe, &writePipe, &attributes, 0)) {
    error = "无法创建命令输出管道（" + win32ErrorText(GetLastError()) + "）";
    return false;
  }
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  std::wstring commandLine = buildCommandLine(args);
  std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
  commandBuffer.push_back(L'\0');
  const std::wstring workDirW = utf8ToWide(workDir);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = writePipe;
  startup.hStdError = writePipe;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION processInfo{};
  const BOOL created =
      CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                     workDirW.empty() ? nullptr : workDirW.c_str(), &startup,
                     &processInfo);
  CloseHandle(writePipe);
  if (!created) {
    const DWORD code = GetLastError();
    CloseHandle(readPipe);
    error = "命令启动失败：" + args[0] + "（" + win32ErrorText(code) + "）";
    return false;
  }
  CloseHandle(processInfo.hThread);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  constexpr size_t kMaxCapture = 2u << 20;  // 最多保留 2MB 输出
  char buffer[65536];
  bool timedOut = false;
  bool processExited = false;
  auto drain = [&]() {
    for (;;) {
      DWORD available = 0;
      if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr))
        break;
      if (available == 0) break;
      DWORD toRead = static_cast<DWORD>(
          std::min<size_t>(available, sizeof(buffer)));
      DWORD got = 0;
      if (!ReadFile(readPipe, buffer, toRead, &got, nullptr) || got == 0) break;
      if (output.size() < kMaxCapture) {
        const size_t room = kMaxCapture - output.size();
        output.append(buffer, std::min<size_t>(got, room));
      }
    }
  };
  for (;;) {
    drain();
    if (WaitForSingleObject(processInfo.hProcess, 15) == WAIT_OBJECT_0) {
      processExited = true;
      drain();
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      timedOut = true;
      taskkillTree(processInfo.dwProcessId);
      TerminateProcess(processInfo.hProcess, 1);
      WaitForSingleObject(processInfo.hProcess, 3000);
      drain();
      break;
    }
  }
  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hProcess);
  CloseHandle(readPipe);
  (void)processExited;

  if (timedOut) {
    error = "命令执行超时（超过 " + std::to_string(timeoutMs) + " ms）";
    return false;
  }
  if (exitCode != 0) {
    error = "命令执行失败（退出码 " + std::to_string(exitCode) + "）";
    return false;
  }
  return true;
}

bool runElevated(const std::vector<std::string>& args, std::string& error) {
  error.clear();
  const std::string self = executablePath();
  if (self.empty()) {
    error = "提权失败：无法定位当前部署器路径";
    return false;
  }
  std::wstring parameters;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) parameters.push_back(L' ');
    parameters += quoteWinArg(utf8ToWide(args[i]));
  }
  const std::wstring selfW = utf8ToWide(self);
  SHELLEXECUTEINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  info.lpVerb = L"runas";
  info.lpFile = selfW.c_str();
  info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    const DWORD code = GetLastError();
    if (code == ERROR_CANCELLED) {
      error = "已取消管理员授权";
    } else if (code == ERROR_ACCESS_DENIED) {
      error = "管理员授权被拒绝";
    } else {
      error = "提权失败（" + win32ErrorText(code) + "）";
    }
    return false;
  }
  if (!info.hProcess) {
    error = "提权失败：未能获取管理员进程句柄";
    return false;
  }
  WaitForSingleObject(info.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(info.hProcess, &exitCode);
  CloseHandle(info.hProcess);
  if (exitCode != 0) {
    error = "管理员操作失败（退出码 " + std::to_string(exitCode) + "）";
    return false;
  }
  return true;
}

bool openBrowser(const std::string& url) {
  if (url.empty()) return false;
  const std::wstring urlW = utf8ToWide(url);
  const HINSTANCE result =
      ShellExecuteW(nullptr, L"open", urlW.c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

void revealPath(const std::string& path) {
  if (path.empty()) return;
  const std::wstring pathW = utf8ToWide(path);
  const DWORD attr = GetFileAttributesW(pathW.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES &&
      (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    ShellExecuteW(nullptr, L"open", pathW.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
    return;
  }
  if (attr != INVALID_FILE_ATTRIBUTES) {
    std::wstring parameters = L"/select,\"";
    parameters += pathW;
    parameters += L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(),
                  nullptr, SW_SHOWNORMAL);
    return;
  }
  const std::wstring parent = parentW(pathW);
  if (!parent.empty())
    ShellExecuteW(nullptr, L"open", parent.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

}  // namespace mes::platform

#endif  // _WIN32
