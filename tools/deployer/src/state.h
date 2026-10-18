#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sc::state {
inline bool buildInput(const std::string &name) {
  return name == "src" || name == "public" || name == "index.html" ||
         name == "package.json" || name == "package-lock.json" ||
         name == "tsconfig.json" || name.rfind("tsconfig.", 0) == 0 ||
         name.rfind("vite.config.", 0) == 0 || name.rfind("postcss.config.", 0) == 0 ||
         name.rfind("tailwind.config.", 0) == 0 || name == ".env" ||
         name.rfind(".env.", 0) == 0;
}
inline bool buildCurrent(uint64_t output, const std::vector<uint64_t> &inputs) {
  if (!output || inputs.empty()) return false;
  for (uint64_t input : inputs)
    if (!input || input > output) return false;
  return true;
}
// Cursors count all emitted records, including records removed from the ring.
inline size_t logBegin(uint64_t cursor, uint64_t first, size_t count) {
  if (cursor < first || cursor > first + count) return 0;
  return (size_t)(cursor - first);
}
inline uint64_t logCursor(const std::string &path) {
  size_t pos = path.find('?');
  while (pos != std::string::npos) {
    const size_t end = path.find('&', pos + 1);
    const std::string part = path.substr(pos + 1, end == std::string::npos ? end : end - pos - 1);
    if (part.rfind("from=", 0) == 0) {
      uint64_t value = 0;
      for (size_t i = 5; i < part.size(); ++i) {
        const unsigned char c = part[i];
        if (c < '0' || c > '9' || value > (std::numeric_limits<uint64_t>::max() - (c - '0')) / 10)
          return 0;
        value = value * 10 + c - '0';
      }
      return value;
    }
    pos = end;
  }
  return 0;
}
}
