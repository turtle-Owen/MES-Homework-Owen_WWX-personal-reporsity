// 校园作业分析 · 桌面部署器 —— Windows HTTP 实现（WinHTTP）。
//
// 支持：302/301/303/307/308 重定向（最多 5 次）、下载进度、取消、超时
// （连接/接收 20s，整体 10 分钟）、先写 .part 再原子改名、错误中文。
#include "platform/platform.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace mes::platform {
namespace {

#ifndef MES_VERSION_STRING
#define MES_VERSION_STRING "0.2.1"
#endif

constexpr int kMaxRedirects = 5;
constexpr int kConnectTimeoutMs = 20000;
constexpr int kReceiveTimeoutMs = 20000;
constexpr auto kTotalTimeout = std::chrono::minutes(10);
constexpr size_t kReadChunk = 64 * 1024;  // 每 64KB 报告一次进度

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      wide.data(), needed);
  return wide;
}

std::string narrow(const std::wstring& text) {
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

std::string winHttpErrorText(DWORD code) {
  switch (code) {
    case ERROR_WINHTTP_TIMEOUT:
      return "连接或下载超时（请检查网络）";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      return "无法解析下载服务器域名（请检查网络或 DNS）";
    case ERROR_WINHTTP_CANNOT_CONNECT:
      return "无法连接下载服务器（请检查网络或代理设置）";
    case ERROR_WINHTTP_CONNECTION_ERROR:
      return "与下载服务器的连接被中断";
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
      return "下载地址无效";
    case ERROR_WINHTTP_SECURE_FAILURE:
      return "HTTPS 证书校验失败（请检查系统时间或网络环境）";
    case ERROR_WINHTTP_AUTODETECTION_FAILED:
      return "网络代理自动检测失败";
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
      return "下载服务器返回了无效响应";
    default:
      return "网络错误（WinHTTP 错误码 " +
             std::to_string(static_cast<unsigned long>(code)) + "）";
  }
}

// HINTERNET 句柄 RAII。
class NetHandle {
 public:
  NetHandle() = default;
  explicit NetHandle(HINTERNET handle) : handle_(handle) {}
  NetHandle(const NetHandle&) = delete;
  NetHandle& operator=(const NetHandle&) = delete;
  NetHandle(NetHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }
  NetHandle& operator=(NetHandle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }
  ~NetHandle() { reset(); }
  void reset() {
    if (handle_) {
      WinHttpCloseHandle(handle_);
      handle_ = nullptr;
    }
  }
  bool valid() const { return handle_ != nullptr; }
  HINTERNET get() const { return handle_; }

 private:
  HINTERNET handle_ = nullptr;
};

struct ParsedUrl {
  bool https = false;
  std::string host;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
  std::string path = "/";
};

bool parseUrl(const std::string& url, ParsedUrl& out, std::string& error) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) {
    error = "下载地址无效：缺少 http:// 或 https:// 前缀";
    return false;
  }
  std::string scheme = url.substr(0, schemeEnd);
  for (char& c : scheme)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  if (scheme != "http" && scheme != "https") {
    error = "不支持的下载地址协议：" + scheme;
    return false;
  }
  out.https = scheme == "https";

  const size_t authorityStart = schemeEnd + 3;
  size_t pathStart = url.size();
  for (size_t i = authorityStart; i < url.size(); ++i) {
    const char c = url[i];
    if (c == '/' || c == '?' || c == '#') {
      pathStart = i;
      break;
    }
  }
  std::string authority = url.substr(authorityStart, pathStart - authorityStart);
  std::string pathAndQuery =
      pathStart == url.size() ? std::string() : url.substr(pathStart);
  if (authority.empty()) {
    error = "下载地址无效：缺少服务器地址";
    return false;
  }
  // 去掉用户信息 user:pass@host
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);
  std::string host = authority;
  std::string portText;
  if (!authority.empty() && authority.front() == '[') {  // IPv6
    const size_t close = authority.find(']');
    if (close == std::string::npos) {
      error = "下载地址无效：IPv6 地址缺少 ]";
      return false;
    }
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':')
      portText = authority.substr(close + 2);
  } else {
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      host = authority.substr(0, colon);
      portText = authority.substr(colon + 1);
    }
  }
  if (host.empty()) {
    error = "下载地址无效：缺少主机名";
    return false;
  }
  out.host = host;
  if (!portText.empty()) {
    int port = 0;
    try {
      port = std::stoi(portText);
    } catch (...) {
      port = 0;
    }
    if (port < 1 || port > 65535) {
      error = "下载地址无效：端口号不合法";
      return false;
    }
    out.port = static_cast<INTERNET_PORT>(port);
  } else {
    out.port = out.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
  }
  // fragment 不参与 HTTP 请求
  const size_t fragment = pathAndQuery.find('#');
  if (fragment != std::string::npos) pathAndQuery.erase(fragment);
  if (pathAndQuery.empty()) {
    out.path = "/";
  } else if (pathAndQuery.front() == '?') {
    out.path = "/" + pathAndQuery;
  } else {
    out.path = pathAndQuery;
  }
  return true;
}

std::string authorityOf(const ParsedUrl& url) {
  std::string authority = url.host;
  const INTERNET_PORT defaultPort =
      url.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
  if (url.port != defaultPort)
    authority += ":" + std::to_string(static_cast<unsigned>(url.port));
  return authority;
}

// 把相对路径按 RFC 3986 的 . / .. 规则规整。
std::string normalizePath(const std::string& path) {
  std::vector<std::string> segments;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t slash = path.find('/', start);
    std::string segment =
        slash == std::string::npos ? path.substr(start)
                                   : path.substr(start, slash - start);
    if (segment == "..") {
      if (!segments.empty()) segments.pop_back();
    } else if (segment != "." && !segment.empty()) {
      segments.push_back(segment);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  std::string result;
  for (const std::string& segment : segments) {
    result += "/";
    result += segment;
  }
  if (result.empty()) result = "/";
  return result;
}

// 根据 302 Location 计算下一个绝对地址；无法解析返回空串。
std::string resolveLocation(const ParsedUrl& base, const std::string& location) {
  if (location.empty()) return {};
  const std::string scheme = base.https ? "https" : "http";
  const std::string prefix = scheme + "://";
  if (location.compare(0, 7, "http://") == 0 ||
      location.compare(0, 8, "https://") == 0)
    return location;
  if (location.size() >= 2 && location[0] == '/' && location[1] == '/')
    return scheme + ":" + location;
  if (location.front() == '/')
    return prefix + authorityOf(base) + location;
  if (location.front() == '?') {
    const size_t question = base.path.find('?');
    const std::string path =
        question == std::string::npos ? base.path : base.path.substr(0, question);
    return prefix + authorityOf(base) + path + location;
  }
  // 相对路径：拼到当前路径所在目录上。
  std::string directory = base.path;
  const size_t lastSlash = directory.find_last_of('/');
  if (lastSlash == std::string::npos)
    directory = "/";
  else
    directory = directory.substr(0, lastSlash + 1);
  return prefix + authorityOf(base) + normalizePath(directory + location);
}

bool queryHeaderString(HINTERNET request, DWORD header, std::wstring& out) {
  out.clear();
  DWORD size = 0;
  WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX,
                      WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(wchar_t))
    return false;
  std::wstring buffer(size / sizeof(wchar_t), L'\0');
  if (!WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX,
                           buffer.data(), &size, WINHTTP_NO_HEADER_INDEX))
    return false;
  buffer.resize(size / sizeof(wchar_t));
  while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
  out = std::move(buffer);
  return true;
}

bool queryHeaderDword(HINTERNET request, DWORD header, DWORD& out) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (!WinHttpQueryHeaders(request, header | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &value, &size,
                           WINHTTP_NO_HEADER_INDEX))
    return false;
  out = value;
  return true;
}

// 从原始响应头里抠 Location（WINHTTP_QUERY_LOCATION 不可用时兜底）。
bool queryRawLocation(HINTERNET request, std::wstring& out) {
  std::wstring raw;
  if (!queryHeaderString(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, raw)) return false;
  size_t start = 0;
  while (start < raw.size()) {
    size_t end = raw.find(L"\r\n", start);
    if (end == std::wstring::npos) end = raw.size();
    std::wstring line = raw.substr(start, end - start);
    if (line.size() > 9) {
      std::wstring lower = line;
      for (wchar_t& c : lower)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
      if (lower.compare(0, 9, L"location:") == 0) {
        size_t valueStart = 9;
        while (valueStart < line.size() &&
               (line[valueStart] == L' ' || line[valueStart] == L'\t'))
          ++valueStart;
        size_t valueEnd = line.size();
        while (valueEnd > valueStart &&
               (line[valueEnd - 1] == L' ' || line[valueEnd - 1] == L'\t'))
          --valueEnd;
        out = line.substr(valueStart, valueEnd - valueStart);
        return !out.empty();
      }
    }
    start = end + 2;
  }
  return false;
}

struct Sink {
  std::function<bool(const char* data, size_t size)> write;
};

bool readResponseBody(HINTERNET request, Sink& sink, uint64_t maxBytes,
                      const DownloadProgress& progress,
                      std::atomic<bool>& cancel, HttpResult& result) {
  uint64_t total = 0;
  DWORD contentLength = 0;
  if (queryHeaderDword(request, WINHTTP_QUERY_CONTENT_LENGTH, contentLength))
    total = contentLength;
  uint64_t received = 0;
  std::vector<char> buffer(kReadChunk);
  const auto deadline = std::chrono::steady_clock::now() + kTotalTimeout;
  if (progress) progress(0, total);
  for (;;) {
    if (cancel.load(std::memory_order_relaxed)) {
      result = {false, result.status, "已取消下载", received};
      return false;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      result = {false, result.status, "下载超时（超过 10 分钟）", received};
      return false;
    }
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      result = {false, result.status,
                "读取下载数据失败（" + winHttpErrorText(GetLastError()) + "）",
                received};
      return false;
    }
    if (available == 0) break;  // 响应体读取完毕
    DWORD toRead = static_cast<DWORD>(
        std::min<size_t>(available, buffer.size()));
    DWORD read = 0;
    if (!WinHttpReadData(request, buffer.data(), toRead, &read)) {
      result = {false, result.status,
                "读取下载数据失败（" + winHttpErrorText(GetLastError()) + "）",
                received};
      return false;
    }
    if (read == 0) break;
    if (maxBytes > 0 && received + read > maxBytes) {
      result = {false, result.status,
                "响应内容过大（超过 " +
                    std::to_string(maxBytes / (1024 * 1024)) + "MB）",
                received};
      return false;
    }
    if (!sink.write(buffer.data(), read)) {
      result = {false, result.status, "写入下载文件失败", received};
      return false;
    }
    received += read;
    if (progress) progress(received, total);
  }
  if (progress && total == 0) progress(received, received);
  result = {true, result.status, "", received};
  return true;
}

HttpResult performRequest(const std::string& rawUrl, Sink& sink,
                          uint64_t maxBytes, const DownloadProgress& progress,
                          std::atomic<bool>& cancel) {
  std::string currentUrl = rawUrl;
  const std::wstring userAgent =
      L"MES-Homework-Deployer/" + widen(MES_VERSION_STRING);
  for (int hop = 0; hop <= kMaxRedirects; ++hop) {
    if (cancel.load(std::memory_order_relaxed))
      return {false, 0, "已取消下载", 0};
    ParsedUrl parsed;
    std::string parseError;
    if (!parseUrl(currentUrl, parsed, parseError))
      return {false, 0, parseError, 0};

    NetHandle session(WinHttpOpen(userAgent.c_str(),
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                  0));
    if (!session.valid())
      return {false, 0,
              "无法初始化 WinHTTP 会话（" + winHttpErrorText(GetLastError()) +
                  "）",
              0};
    WinHttpSetTimeouts(session.get(), kConnectTimeoutMs, kConnectTimeoutMs,
                       kConnectTimeoutMs, kReceiveTimeoutMs);
    const std::wstring hostW = widen(parsed.host);
    NetHandle connect(
        WinHttpConnect(session.get(), hostW.c_str(), parsed.port, 0));
    if (!connect.valid())
      return {false, 0,
              "无法连接下载服务器：" + parsed.host + "（" +
                  winHttpErrorText(GetLastError()) + "）",
              0};
    const std::wstring pathW = widen(parsed.path);
    const DWORD requestFlags = parsed.https ? WINHTTP_FLAG_SECURE : 0;
    NetHandle request(WinHttpOpenRequest(
        connect.get(), L"GET", pathW.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags));
    if (!request.valid())
      return {false, 0,
              "无法创建 HTTP 请求（" + winHttpErrorText(GetLastError()) + "）",
              0};
    // 关闭 WinHTTP 的自动重定向：由本函数统一处理 3xx（限制 5 次、解析
    // Location、支持相对地址），避免底层策略与进度/取消逻辑打架。
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirectPolicy, sizeof(redirectPolicy));
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
      return {false, 0,
              "请求下载失败（" + winHttpErrorText(GetLastError()) + "）", 0};
    if (!WinHttpReceiveResponse(request.get(), nullptr))
      return {false, 0,
              "接收下载响应失败（" + winHttpErrorText(GetLastError()) + "）",
              0};

    DWORD status = 0;
    if (!queryHeaderDword(request.get(), WINHTTP_QUERY_STATUS_CODE, status))
      return {false, 0, "无法读取 HTTP 状态码", 0};

    const bool isRedirect = status == 301 || status == 302 || status == 303 ||
                            status == 307 || status == 308;
    if (isRedirect) {
      if (hop == kMaxRedirects)
        return {false, static_cast<int>(status), "下载失败：重定向次数过多（超过 5 次）",
                0};
      std::wstring location;
      if (!queryHeaderString(request.get(), WINHTTP_QUERY_LOCATION, location) ||
          location.empty()) {
        queryRawLocation(request.get(), location);
      }
      if (location.empty())
        return {false, static_cast<int>(status),
                "下载失败：服务器返回重定向但缺少 Location 头", 0};
      const std::string next = resolveLocation(parsed, narrow(location));
      if (next.empty())
        return {false, static_cast<int>(status), "下载失败：无法解析重定向地址", 0};
      currentUrl = next;
      continue;  // 句柄在循环作用域结束时关闭
    }
    if (status < 200 || status >= 300)
      return {false, static_cast<int>(status),
              "下载失败：HTTP " + std::to_string(status), 0};

    HttpResult result{false, static_cast<int>(status), "", 0};
    readResponseBody(request.get(), sink, maxBytes, progress, cancel, result);
    return result;
  }
  return {false, 0, "下载失败：重定向次数过多（超过 5 次）", 0};
}

}  // namespace

HttpResult httpDownload(const std::string& url, const std::string& destPath,
                        DownloadProgress progress, std::atomic<bool>& cancel) {
  if (url.empty()) return {false, 0, "下载地址为空", 0};
  if (destPath.empty()) return {false, 0, "下载目标路径为空", 0};
  const std::wstring destW = widen(destPath);
  std::wstring partW = destW + L".part";
  DeleteFileW(partW.c_str());
  HANDLE file = CreateFileW(partW.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return {false, 0, "无法创建下载临时文件：" + destPath + ".part", 0};

  Sink sink;
  sink.write = [file](const char* data, size_t size) -> bool {
    size_t offset = 0;
    while (offset < size) {
      const DWORD chunk = static_cast<DWORD>(
          std::min<size_t>(size - offset, 1u << 20));
      DWORD written = 0;
      if (!WriteFile(file, data + offset, chunk, &written, nullptr) ||
          written != chunk)
        return false;
      offset += written;
    }
    return true;
  };
  HttpResult result = performRequest(url, sink, 0, progress, cancel);
  FlushFileBuffers(file);
  CloseHandle(file);
  if (!result.ok) {
    DeleteFileW(partW.c_str());  // 取消/失败：清理半成品
    return result;
  }
  if (!MoveFileExW(partW.c_str(), destW.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    // Windows 不允许覆盖只读/占用文件时，再尝试删一次目标。
    DeleteFileW(destW.c_str());
    if (!MoveFileExW(partW.c_str(), destW.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      DeleteFileW(partW.c_str());
      return {false, result.status, "无法保存下载文件：" + destPath, result.bytes};
    }
  }
  return result;
}

HttpResult httpGet(const std::string& url, std::string& body,
                   std::atomic<bool>& cancel) {
  body.clear();
  if (url.empty()) return {false, 0, "请求地址为空", 0};
  Sink sink;
  sink.write = [&body](const char* data, size_t size) -> bool {
    body.append(data, size);
    return true;
  };
  HttpResult result = performRequest(url, sink, 2ull * 1024 * 1024, nullptr,
                                     cancel);
  if (!result.ok) body.clear();
  return result;
}

}  // namespace mes::platform

#endif  // _WIN32
