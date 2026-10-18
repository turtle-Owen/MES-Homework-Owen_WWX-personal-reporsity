#include "../src/state.h"
#include <cassert>
#include <iostream>
int main() {
  for (const char *name : {"src", "public", "index.html", "package.json", "package-lock.json", "vite.config.ts", "tsconfig.app.json", ".env.production"})
    assert(sc::state::buildInput(name));
  assert(!sc::state::buildInput("data"));
  assert(!sc::state::buildInput("node_modules"));
  assert(sc::state::buildCurrent(100, {20, 99}));
  assert(!sc::state::buildCurrent(100, {20, 101}));
  assert(!sc::state::buildCurrent(0, {20}));
  assert(!sc::state::buildCurrent(100, {}));
  assert(sc::state::logCursor("/api/status?from=150&deps=1") == 150);
  assert(sc::state::logCursor("/api/status?deps=1&from=450") == 450);
  assert(sc::state::logCursor("/api/status?from=12oops") == 0);
  assert(sc::state::logCursor("/api/status?from=18446744073709551616") == 0);
  assert(sc::state::logBegin(450, 200, 400) == 250);
  assert(sc::state::logBegin(600, 200, 400) == 400);
  assert(sc::state::logBegin(100, 200, 400) == 0);
  assert(sc::state::logBegin(700, 200, 400) == 0);
  std::cout << "Deployment build freshness and incremental log regressions passed\n";
}
