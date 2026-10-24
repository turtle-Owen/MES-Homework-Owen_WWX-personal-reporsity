// 校园作业分析 · 桌面部署器 —— POSIX HTTP 实现。
//
// 优先 dlopen libcurl（无编译期依赖）：跟随重定向、进度、取消、超时；
// libcurl 不可用时回退 fork/exec `curl` / `wget`；都没有则给中文错误。
#include "platform/platform.h"

#if !defined(_WIN32)

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace mes::platform {
namespace {

// libcurl 的 ABI 常量（curl/curl.h 中数值长期稳定；dlopen 场景直接使用）。
constexpr int kCurlOptWriteData = 10001;
constexpr int kCurlOptUrl = 10002;
constexpr int kCurlOptUserAgent = 10018;
constexpr int kCurlOptFollowLocation = 52;
constexpr int kCurlOptMaxRedirs = 68;
constexpr int kCurlOptNoSignal = 99;
constexpr int kCurlOptNoProgress = 43;
constexpr int kCurlOptXferInfoFunction = 20219;
constexpr int kCurlOptXferInfoData = 10057;
constexpr int kCurlOptTimeoutMs = 155;
constexpr int kCurlOptConnectTimeoutMs = 156;
constexpr int kCurlOptWriteFunction = 20011;
constexpr int kCurlInfoResponseCode = 0x200002;
constexpr long kCurlGlobalAll = 3;
constexpr size_t kMaxBodyBytes = 2ull * 1024 * 1024;

using CurlOffT = long long;
using CurlWriteFn = size_t (*)(char*, size_t, size_t, void*);
using CurlXferInfoFn = int (*)(void*, CurlOffT, CurlOffT, CurlOffT, CurlOffT);

struct CurlApi {
  void* handle = nullptr;
  void* (*easyInit)() = nullptr;
  int (*easySetopt)(void*, int, ...) = nullptr;
  int (*easyPerform)(void*) = nullptr;
  void (*easyCleanup)(void*) = nullptr;
  const char* (*easyStrerror)(int) = nullptr;
  int (*easyGetinfo)(void*, int, ...) = nullptr;
  int (*globalInit)(long) = nullptr;

  bool loaded() const {
    return handle && easyInit && easySetopt && easyPerform && easyCleanup;
  }
};

CurlApi loadCurlApi() {
  CurlApi api;
  static const char* const kCandidates[] = {
      "libcurl.so.4",
      "libcurl.so",
      "libcurl.4.dylib",
      "libcurl.dylib",
      "/usr/lib/libcurl.4.dylib",           // macOS 系统自带
      "/usr/lib/libcurl.dylib",
      "/opt/homebrew/opt/curl/lib/libcurl.4.dylib",   // Apple Silicon Homebrew
      "/usr/local/opt/curl/lib/libcurl.4.dylib",      // Intel Homebrew
  };
  for (const char* name : kCandidates) {
    void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!handle) continue;
    api.easyInit = reinterpret_cast<decltype(api.easyInit)>(
        dlsym(handle, "curl_easy_init"));
    api.easySetopt = reinterpret_cast<decltype(api.easySetopt)>(
        dlsym(handle, "curl_easy_setopt"));
    api.easyPerform = reinterpret_cast<decltype(api.easyPerform)>(
        dlsym(handle, "curl_easy_perform"));
    api.easyCleanup = reinterpret_cast<decltype(api.easyCleanup)>(
        dlsym(handle, "curl_easy_cleanup"));
    api.easyStrerror = reinterpret_cast<decltype(api.easyStrerror)>(
        dlsym(handle, "curl_easy_strerror"));
    api.easyGetinfo = reinterpret_cast<decltype(api.easyGetinfo)>(
        dlsym(handle, "curl_easy_getinfo"));
    api.globalInit =
        reinterpret_cast<decltype(api.globalInit)>(dlsym(handle, "curl_global_init"));
    if (api.loaded()) {
      api.handle = handle;
      return api;
    }
    dlclose(handle);
    api = CurlApi{};
  }
  return api;
}

CurlApi& curlApi() {
  static CurlApi api = loadCurlApi();
  return api;
}

void ensureCurlGlobalInit() {
  static std::once_flag flag;
  std::call_once(flag, []() {
    CurlApi& api = curlApi();
    if (api.loaded() && api.globalInit) api.globalInit(kCurlGlobalAll);
  });
}

std::string curlErrorText(const CurlApi& api, int code) {
  switch (code) {
    case 5:
      return "无法解析代理服务器（请检查代理设置）";
    case 6:
      return "无法解析服务器域名（请检查网络或 DNS）";
    case 7:
      return "无法连接服务器（请检查网络或代理设置）";
    case 22:
      return "服务器返回了无效响应（可能不支持断点/重定向）";
    case 23:
      return "写入下载文件失败（磁盘空间不足或权限不足）";
    case 28:
      return "下载超时（请检查网络）";
    case 35:
      return "TLS 握手失败（请检查系统时间与网络环境）";
    case 42:
      return "已取消下载";
    case 51:
    case 58:
    case 60:
    case 77:
    case 83:
      return "HTTPS 证书校验失败（请检查系统时间或网络环境）";
    default: {
      std::string text = "网络请求失败";
      if (api.easyStrerror) {
        const char* message = api.easyStrerror(code);
        if (message && message[0] != '\0') {
          text += "：";
          text += message;
        }
      }
      return text;
    }
  }
}

struct CurlCallResult {
  bool attempted = false;
  bool ok = false;
  int status = 0;
  int curlCode = 0;
};

CurlCallResult performCurl(const std::string& url, CurlWriteFn writeFn,
                           void* writeData, CurlXferInfoFn progressFn,
                           void* progressData) {
  CurlCallResult result;
  CurlApi& api = curlApi();
  if (!api.loaded()) return result;
  ensureCurlGlobalInit();
  void* easy = api.easyInit();
  if (!easy) return result;
  result.attempted = true;
  api.easySetopt(easy, kCurlOptUrl, url.c_str());
  api.easySetopt(easy, kCurlOptFollowLocation, 1L);
  api.easySetopt(easy, kCurlOptMaxRedirs, 5L);
  api.easySetopt(easy, kCurlOptNoSignal, 1L);
  api.easySetopt(easy, kCurlOptConnectTimeoutMs, 20000L);
  api.easySetopt(easy, kCurlOptTimeoutMs, 600000L);
  api.easySetopt(easy, kCurlOptWriteFunction, writeFn);
  api.easySetopt(easy, kCurlOptWriteData, writeData);
  api.easySetopt(easy, kCurlOptUserAgent, "MES-Homework-Deployer/0.2.1");
  if (progressFn) {
    api.easySetopt(easy, kCurlOptNoProgress, 0L);
    api.easySetopt(easy, kCurlOptXferInfoFunction, progressFn);
    api.easySetopt(easy, kCurlOptXferInfoData, progressData);
  } else {
    api.easySetopt(easy, kCurlOptNoProgress, 1L);
  }
  result.curlCode = api.easyPerform(easy);
  long status = 0;
  if (api.easyGetinfo) api.easyGetinfo(easy, kCurlInfoResponseCode, &status);
  result.status = static_cast<int>(status);
  api.easyCleanup(easy);
  result.ok = result.curlCode == 0;
  return result;
}

// ---------------------------------------------------------------- 下载 sink
struct DownloadContext {
  int fd = -1;
  std::atomic<bool>* cancel = nullptr;
  DownloadProgress progress;
  uint64_t received = 0;
  bool cancelled = false;
  bool writeFailed = false;
  std::chrono::steady_clock::time_point lastReport{};
};

size_t downloadWrite(char* data, size_t size, size_t count, void* userdata) {
  auto* context = static_cast<DownloadContext*>(userdata);
  const size_t bytes = size * count;
  if (context->cancel &&
      context->cancel->load(std::memory_order_relaxed)) {
    context->cancelled = true;
    return 0;
  }
  size_t offset = 0;
  while (offset < bytes) {
    const ssize_t written =
        ::write(context->fd, data + offset, bytes - offset);
    if (written < 0) {
      if (errno == EINTR) continue;
      context->writeFailed = true;
      return 0;
    }
    offset += static_cast<size_t>(written);
  }
  context->received += bytes;
  return bytes;
}

int downloadProgress(void* clientp, CurlOffT totalDownload, CurlOffT downloaded,
                     CurlOffT totalUpload, CurlOffT uploaded) {
  (void)totalUpload;
  (void)uploaded;
  auto* context = static_cast<DownloadContext*>(clientp);
  if (context->cancel && context->cancel->load()) {
    context->cancelled = true;
    return 1;  // 非 0 中断传输
  }
  if (!context->progress) return 0;
  const auto now = std::chrono::steady_clock::now();
  const bool finished = totalDownload > 0 && downloaded >= totalDownload;
  if (!finished && now - context->lastReport < std::chrono::milliseconds(100))
    return 0;
  context->lastReport = now;
  const uint64_t total = totalDownload > 0
                             ? static_cast<uint64_t>(totalDownload)
                             : 0;
  const uint64_t current = downloaded > 0 ? static_cast<uint64_t>(downloaded) : 0;
  context->progress(current, total);
  return 0;
}

// ---------------------------------------------------------------- 字符串 sink
struct BufferContext {
  std::string* body = nullptr;
  size_t maxBytes = kMaxBodyBytes;
  std::atomic<bool>* cancel = nullptr;
  bool cancelled = false;
  bool tooLarge = false;
};

size_t bufferWrite(char* data, size_t size, size_t count, void* userdata) {
  auto* context = static_cast<BufferContext*>(userdata);
  const size_t bytes = size * count;
  if (context->cancel && context->cancel->load()) {
    context->cancelled = true;
    return 0;
  }
  if (context->body->size() + bytes > context->maxBytes) {
    context->tooLarge = true;
    return 0;
  }
  context->body->append(data, bytes);
  return bytes;
}

// ---------------------------------------------------------------- CLI 回退
std::string findExecutableInPath(const char* name) {
  const char* pathEnvironment = std::getenv("PATH");
  if (!pathEnvironment) return {};
  const std::string path = pathEnvironment;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t colon = path.find(':', start);
    std::string directory = colon == std::string::npos
                                ? path.substr(start)
                                : path.substr(start, colon - start);
    if (directory.empty()) directory = ".";
    const std::string candidate = directory + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
    if (colon == std::string::npos) break;
    start = colon + 1;
  }
  return {};
}

HttpResult downloadViaCli(const std::string& url, const std::string& destPath,
                          const DownloadProgress& progress,
                          std::atomic<bool>& cancel) {
  if (cancel.load()) return {false, 0, "已取消下载", 0};
  std::string part = destPath + ".part-cli";
  if (progress) progress(0, 0);
  std::string output;
  std::string error;
  bool ok = false;
  const std::string curl = findExecutableInPath("curl");
  if (!curl.empty()) {
    ok = runCommand({curl, "-fL", "--connect-timeout", "20", "--max-time",
                     "600", "-o", part, url},
                    "", output, error, 660000);
  } else {
    const std::string wget = findExecutableInPath("wget");
    if (wget.empty())
      return {false, 0, "未找到 libcurl/curl/wget，无法下载（请安装 curl）", 0};
    ok = runCommand({wget, "-q", "--timeout=20", "--tries=2", "-O", part, url},
                    "", output, error, 660000);
  }
  if (cancel.load()) {
    std::remove(part.c_str());
    return {false, 0, "已取消下载", 0};
  }
  if (!ok) {
    std::remove(part.c_str());
    return {false, 0,
            "下载失败：" + (error.empty() ? output : error), 0};
  }
  std::remove(destPath.c_str());
  if (std::rename(part.c_str(), destPath.c_str()) != 0) {
    std::remove(part.c_str());
    return {false, 0, "无法保存下载文件：" + destPath, 0};
  }
  const uint64_t bytes = fileSize(destPath);
  if (progress) progress(bytes, bytes);
  return {true, 200, "", bytes};
}

HttpResult getViaCli(const std::string& url, std::string& body,
                     std::atomic<bool>& cancel) {
  if (cancel.load()) return {false, 0, "已取消下载", 0};
  std::string output;
  std::string error;
  bool ok = false;
  const std::string curl = findExecutableInPath("curl");
  if (!curl.empty()) {
    ok = runCommand({curl, "-fsSL", "--connect-timeout", "20", "--max-time",
                     "120", url},
                    "", output, error, 130000);
  } else {
    const std::string wget = findExecutableInPath("wget");
    if (wget.empty())
      return {false, 0, "未找到 libcurl/curl/wget，无法访问网络", 0};
    ok = runCommand({wget, "-qO-", "--timeout=20", "--tries=2", url}, "",
                    output, error, 130000);
  }
  if (cancel.load()) return {false, 0, "已取消下载", 0};
  if (!ok)
    return {false, 0,
            "请求失败：" + (error.empty() ? output : error), 0};
  if (output.size() > kMaxBodyBytes)
    return {false, 0, "响应内容过大（超过 2MB）", 0};
  body = std::move(output);
  return {true, 200, "", body.size()};
}

}  // namespace

HttpResult httpDownload(const std::string& url, const std::string& destPath,
                        DownloadProgress progress, std::atomic<bool>& cancel) {
  if (url.empty()) return {false, 0, "下载地址为空", 0};
  if (destPath.empty()) return {false, 0, "下载目标路径为空", 0};
  if (cancel.load()) return {false, 0, "已取消下载", 0};

  CurlApi& api = curlApi();
  if (!api.loaded()) return downloadViaCli(url, destPath, progress, cancel);

  const std::string part = destPath + ".part";
  std::remove(part.c_str());
  const int fd = ::open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return {false, 0, "无法创建下载临时文件：" + part, 0};
  DownloadContext context;
  context.fd = fd;
  context.cancel = &cancel;
  context.progress = progress;
  const CurlCallResult call = performCurl(url, &downloadWrite, &context,
                                          &downloadProgress, &context);
  ::close(fd);
  if (!call.attempted) {
    std::remove(part.c_str());
    return downloadViaCli(url, destPath, progress, cancel);
  }
  if (context.cancelled || call.curlCode == 42) {
    std::remove(part.c_str());
    return {false, 0, "已取消下载", context.received};
  }
  if (!call.ok) {
    std::remove(part.c_str());
    return {false, call.status, curlErrorText(api, call.curlCode),
            context.received};
  }
  if (call.status < 200 || call.status >= 300) {
    std::remove(part.c_str());
    return {false, call.status,
            "下载失败：HTTP " + std::to_string(call.status), context.received};
  }
  std::remove(destPath.c_str());
  if (std::rename(part.c_str(), destPath.c_str()) != 0) {
    std::remove(part.c_str());
    return {false, call.status, "无法保存下载文件：" + destPath,
            context.received};
  }
  if (progress) progress(context.received, context.received);
  return {true, call.status, "", context.received};
}

HttpResult httpGet(const std::string& url, std::string& body,
                   std::atomic<bool>& cancel) {
  body.clear();
  if (url.empty()) return {false, 0, "请求地址为空", 0};
  if (cancel.load()) return {false, 0, "已取消下载", 0};

  CurlApi& api = curlApi();
  if (!api.loaded()) return getViaCli(url, body, cancel);

  BufferContext context;
  context.body = &body;
  context.cancel = &cancel;
  const CurlCallResult call =
      performCurl(url, &bufferWrite, &context, nullptr, nullptr);
  if (!call.attempted) return getViaCli(url, body, cancel);
  if (context.cancelled || call.curlCode == 42) {
    body.clear();
    return {false, 0, "已取消下载", 0};
  }
  if (!call.ok) {
    body.clear();
    return {false, call.status, curlErrorText(api, call.curlCode), 0};
  }
  if (call.status < 200 || call.status >= 300) {
    body.clear();
    return {false, call.status,
            "请求失败：HTTP " + std::to_string(call.status), 0};
  }
  if (context.tooLarge) {
    body.clear();
    return {false, call.status, "响应内容过大（超过 2MB）", 0};
  }
  return {true, call.status, "", body.size()};
}

}  // namespace mes::platform

#endif  // !_WIN32
