// 校园作业分析 · 桌面部署器 —— SHA-256 实现（FIPS 180-4）
// 用于校验下载的安装包与自检；不依赖任何第三方密码学库。
#include "core/core.h"
#include "platform/platform.h"

#include <cstring>
#include <string>
#include <vector>

namespace mes::core {
namespace {

// ---------------------------------------------------------------- SHA-256 常量
constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
 public:
  Sha256() { reset(); }

  void reset() {
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
    bufferSize_ = 0;
    totalBits_ = 0;
  }

  void update(const void* data, size_t size) {
    if (data == nullptr || size == 0) return;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    totalBits_ += static_cast<uint64_t>(size) * 8u;
    size_t offset = 0;
    while (offset < size) {
      const size_t room = 64 - bufferSize_;
      const size_t take = (size - offset) < room ? (size - offset) : room;
      std::memcpy(buffer_ + bufferSize_, bytes + offset, take);
      bufferSize_ += take;
      offset += take;
      if (bufferSize_ == 64) {
        transform(buffer_);
        bufferSize_ = 0;
      }
    }
  }

  // 返回 32 字节摘要。
  std::string finalize() {
    // 追加 0x80，再补零到 56 mod 64，最后 8 字节大端写入消息比特长度。
    unsigned char pad = 0x80;
    updateRaw(&pad, 1);
    unsigned char zero = 0x00;
    while (bufferSize_ != 56) updateRaw(&zero, 1);
    uint64_t remainingBits = totalBits_;
    unsigned char length[8];
    for (int i = 7; i >= 0; --i) {
      length[i] = static_cast<unsigned char>(remainingBits & 0xffu);
      remainingBits >>= 8;
    }
    updateRaw(length, 8);
    std::string digest(32, '\0');
    for (int i = 0; i < 8; ++i) {
      digest[i * 4 + 0] = static_cast<char>((state_[i] >> 24) & 0xffu);
      digest[i * 4 + 1] = static_cast<char>((state_[i] >> 16) & 0xffu);
      digest[i * 4 + 2] = static_cast<char>((state_[i] >> 8) & 0xffu);
      digest[i * 4 + 3] = static_cast<char>(state_[i] & 0xffu);
    }
    reset();
    return digest;
  }

 private:
  uint32_t state_[8];
  unsigned char buffer_[64];
  size_t bufferSize_;
  uint64_t totalBits_;

  // 填充阶段使用：不再累计比特长度，也不做 64 字节块的“满块”循环外逻辑。
  void updateRaw(const unsigned char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      buffer_[bufferSize_++] = data[i];
      if (bufferSize_ == 64) {
        transform(buffer_);
        bufferSize_ = 0;
      }
    }
  }

  void transform(const unsigned char* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
             (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const uint32_t ch = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
      const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
};

std::string toHex(const std::string& digest) {
  static const char kHexDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(digest.size() * 2);
  for (char raw : digest) {
    const unsigned char byte = static_cast<unsigned char>(raw);
    hex.push_back(kHexDigits[byte >> 4]);
    hex.push_back(kHexDigits[byte & 0x0f]);
  }
  return hex;
}

}  // namespace

std::string sha256Hex(const void* data, size_t size) {
  Sha256 sha;
  sha.update(data, size);
  return toHex(sha.finalize());
}

std::string sha256HexFile(const std::string& path) {
  if (path.empty()) return {};
  std::string content;
  if (!mes::platform::readFile(path, content)) return {};
  return sha256Hex(content.data(), content.size());
}

}  // namespace mes::core
