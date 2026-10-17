#include "../src/security.h"
#include "../src/json.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace sc::security;
int main() {
  assert(browserRequest("127.0.0.1:8899", "", "GET", 8899));
  assert(browserRequest("127.0.0.1:8899", "http://127.0.0.1:8899", "POST", 8899));
  assert(!browserRequest("attacker.test:8899", "http://attacker.test:8899", "POST", 8899));
  assert(!browserRequest("127.0.0.1:8899", "https://attacker.test", "POST", 8899));
  assert(!browserRequest("127.0.0.1:8899", "null", "POST", 8899));
  assert(!browserRequest("127.0.0.1:8899", "", "POST", 8899));
  for (const std::string bad : {"abc & calc", "abc%PATH%", "x\r\noutput=bad", "x\"", "x!PATH!", "x|calc", "x^y"}) {
    assert(!token(bad));
    assert(!shellPath(bad));
  }
  assert(token("aGVsbG8+/=_-abc.123"));
  assert(domain("homework.example.com"));
  assert(!domain("example.com\ningress: bad"));
  assert(!domain("-bad.com"));
  assert(!domain("bad..com"));
  assert(repo("https://github.com/turtlelnc/MES-Homework.git"));
  assert(!repo("https://github.com/x & calc"));
  assert(!shellPath("C:\\a%PATH%\\b"));
  assert(shellPath("C:\\Users\\teacher\\School Project"));
  assert(remainingRequestMs(1000, 6000) == requestTimeoutMs);
  assert(remainingRequestMs(5999, 6000) == 1);
  assert(remainingRequestMs(6000, 6000) == 0);
  assert(remainingRequestMs(6001, 6000) == 0);
  size_t n = 0;
  assert(contentLength("16384", n) && n == maxBody);
  for (const std::string bad : {"16385", "999999999999999999999999999", "-1", "1x", ""}) assert(!contentLength(bad, n));
  std::atomic<bool> busy{true};
  assert(!mayStop(busy));
  { BusyReset guard{busy}; }
  assert(mayStop(busy));
  busy = true;
  try { BusyReset guard{busy}; throw std::runtime_error("failure"); } catch (...) {}
  assert(mayStop(busy));
  sc::Json value;
  assert(!sc::Json::parse(std::string(1000, '[') + "0" + std::string(1000, ']'), value));
  assert(sc::Json::parse("{\"port\":8787}", value));
  std::cout << "Deployment security regression checks passed\n";
}
