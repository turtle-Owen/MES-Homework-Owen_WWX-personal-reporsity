#pragma once
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sc::security {
constexpr size_t maxHeaders = 16384;
constexpr size_t maxBody = 16384;
constexpr unsigned requestTimeoutMs = 5000;
inline unsigned remainingRequestMs(uint64_t now, uint64_t deadline) {
  return now >= deadline ? 0 : (unsigned)(deadline - now);
}
inline bool browserRequest(const std::string &host, const std::string &origin,
                           const std::string &method, int port) {
  const std::string expected = "127.0.0.1:" + std::to_string(port);
  return host == expected && (origin == "http://" + expected ||
                             (origin.empty() && method == "GET"));
}
inline bool token(const std::string &text) {
  if (text.size() > 8192) return false;
  for (unsigned char c : text)
    if (!(std::isalnum(c) || c == '_' || c == '-' || c == '+' || c == '/' || c == '=' || c == '.')) return false;
  return true;
}
inline bool domain(const std::string &text) {
  if (text.empty()) return true;
  if (text.size() > 253 || text.front() == '.' || text.back() == '.') return false;
  size_t label = 0;
  char previous = '.';
  for (unsigned char c : text) {
    if (c == '.') {
      if (!label || label > 63 || previous == '-') return false;
      label = 0;
    } else {
      if (!(std::isalnum(c) || c == '-') || (!label && c == '-')) return false;
      ++label;
    }
    previous = c;
  }
  return label && label <= 63 && previous != '-';
}
inline bool shellPath(const std::string &text) {
  return text.find_first_of("\"'%!?^&|<>\r\n") == std::string::npos;
}
inline bool repo(const std::string &text) {
  if (text.rfind("https://", 0) != 0) return false;
  for (unsigned char c : text)
    if (!(std::isalnum(c) || c == ':' || c == '/' || c == '.' || c == '_' || c == '-')) return false;
  return true;
}
inline bool mayStop(const std::atomic<bool> &busy) { return !busy.load(); }
inline bool contentLength(const std::string &text, size_t &value) {
  value = 0;
  if (text.empty()) return false;
  for (unsigned char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + c - '0';
    if (value > maxBody) return false;
  }
  return true;
}
struct BusyReset {
  std::atomic<bool> &busy;
  ~BusyReset() { busy = false; }
};
}  // namespace sc::security
