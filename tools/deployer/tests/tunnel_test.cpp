#include "../src/cloudflare.h"
#include "../src/util.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {
std::string responseBody, requestBody, written, writtenPath;
bool writeOk = true;
int requests = 0, writes = 0;
const std::string id = "11111111-2222-3333-4444-555555555555";
void reset(const std::string &body) {
  responseBody = body;
  requestBody.clear(); written.clear(); writtenPath.clear();
  requests = writes = 0; writeOk = true;
}
std::string success(const std::string &tunnelId) {
  return "{\"success\":true,\"result\":{\"id\":\"" + tunnelId + "\"}}";
}
}

// Link-time adapters: no HTTP requests, filesystem writes or live secrets.
namespace sc {
std::string randomHex(int bytes) { assert(bytes == 32); return std::string(64, 'a'); }
std::string jsonEscape(const std::string &text) {
  std::string escaped;
  for (char ch : text) {
    if (ch == '\\' || ch == '"') escaped += '\\';
    escaped += ch;
  }
  return escaped;
}
std::string urlEncode(const std::string &text) { return text; }
HttpResponse httpPostJson(const std::string &url, const std::string &body,
                          const std::vector<std::string> &headers) {
  assert(url == "https://api.cloudflare.com/client/v4/accounts/account123/cfd_tunnel");
  assert(headers.size() == 1 && headers[0] == "authorization: Bearer test-token");
  ++requests; requestBody = body;
  return HttpResponse{true, 200, responseBody, "", true};
}
HttpResponse httpGet(const std::string &, const std::vector<std::string> &) {
  throw std::runtime_error("Unexpected GET");
}
HttpResponse httpRequest(const std::string &, const std::string &,
                         const std::vector<std::string> &, const std::string &) {
  throw std::runtime_error("Unexpected DNS mutation");
}
bool writeFileUtf8(const std::string &path, const std::string &content) {
  ++writes; writtenPath = path; written = content; return writeOk;
}
}

int main() {
  assert(!sc::cf::registeredTunnel("ERR Failed to dial a quic connection connIndex=0 error=timeout"));
  assert(!sc::cf::registeredTunnel("INF Retrying connection in up to 2s connIndex=0"));
  assert(!sc::cf::registeredTunnel(""));
  assert(sc::cf::registeredTunnel("INF Registered tunnel connection connIndex=0 protocol=quic"));
  assert(sc::cf::registeredTunnel("INF Connection registered"));
  reset(success(id));
  auto result = sc::cf::createLocalTunnel("test-token", "account123", "tunnel.json");
  assert(result.ok && result.data["id"].asString() == id);
  assert(requests == 1 && writes == 1 && writtenPath == "tunnel.json");
  sc::Json request, credentials;
  assert(sc::Json::parse(requestBody, request) && sc::Json::parse(written, credentials));
  assert(request["config_src"].asString() == "local");
  assert(credentials["AccountTag"].asString() == "account123");
  assert(credentials["TunnelID"].asString() == id);
  const std::string secret = request["tunnel_secret"].asString();
  // Known RFC 4648 base64 of 64 ASCII 'a' bytes; validates padding too.
  assert(secret == "YWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYQ==");
  assert(credentials["TunnelSecret"].asString() == secret);
  assert(result.message.find(secret) == std::string::npos);

  reset(success(id)); writeOk = false;
  result = sc::cf::createLocalTunnel("test-token", "account123", "tunnel.json");
  assert(!result.ok && requests == 1 && writes == 1);
  assert(result.message.find(secret) == std::string::npos);

  reset("{\"success\":false,\"errors\":[{\"message\":\"denied\"}]}");
  result = sc::cf::createLocalTunnel("test-token", "account123", "tunnel.json");
  assert(!result.ok && requests == 1 && writes == 0);

  for (const std::string invalid : {"", "id&calc", "../other"}) {
    reset(success(invalid));
    result = sc::cf::createLocalTunnel("test-token", "account123", "tunnel.json");
    assert(!result.ok && writes == 0);
  }
  reset("not json");
  result = sc::cf::createLocalTunnel("test-token", "account123", "tunnel.json");
  assert(!result.ok && writes == 0);

  reset(success(id));
  result = sc::cf::createLocalTunnel("test-token", "", "tunnel.json");
  assert(!result.ok && requests == 0 && writes == 0);
  std::cout << "Tunnel provisioning regression checks passed (8 provisioning scenarios + 5 connection logs, mocked I/O)\n";
}
