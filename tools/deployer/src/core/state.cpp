// 校园作业分析 · 桌面部署器 —— 安装状态读写（~/.mes-homework-deployer.json）
#include "core/core.h"
#include "core/core_util.h"
#include "core/json.h"
#include "platform/platform.h"

#include <string>

namespace mes::core {
namespace {

// 手写安全 JSON 转义：控制字符与引号/反斜杠转义，UTF-8 字节原样保留。
std::string jsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  static const char kHex[] = "0123456789abcdef";
  for (char raw : text) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20) {
          out += "\\u00";
          out.push_back(kHex[(ch >> 4) & 0x0f]);
          out.push_back(kHex[ch & 0x0f]);
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  return out;
}

std::string quoted(const std::string& value) {
  return "\"" + jsonEscape(value) + "\"";
}

}  // namespace

std::string installStateToJson(const InstallState& state) {
  std::string json;
  json += "{\n";
  json += "  \"version\": " + quoted(state.version) + ",\n";
  json += "  \"edition\": " + quoted(state.edition) + ",\n";
  json += "  \"installDir\": " + quoted(state.installDir) + ",\n";
  json += "  \"dataDir\": " + quoted(state.dataDir) + ",\n";
  json += "  \"port\": " + std::to_string(state.port) + ",\n";
  json += "  \"installedAt\": " + quoted(state.installedAt) + ",\n";
  json += "  \"bundleVersion\": " + quoted(state.bundleVersion) + ",\n";
  json += "  \"lastAction\": " + quoted(state.lastAction) + "\n";
  json += "}\n";
  return json;
}

bool installStateFromJson(const std::string& json, InstallState& out) {
  sc::Json root;
  if (!sc::Json::parse(json, root, nullptr)) return false;
  if (!root.isObject()) return false;
  InstallState state;
  state.version = root["version"].isString() ? root["version"].asString() : "";
  state.edition = root["edition"].isString() ? root["edition"].asString() : "";
  if (state.edition != "local" && state.edition != "server") state.edition = "";
  state.installDir =
      root["installDir"].isString() ? root["installDir"].asString() : "";
  state.dataDir = root["dataDir"].isString() ? root["dataDir"].asString() : "";
  const sc::Json& port = root["port"];
  if (port.isNumber()) {
    const double value = port.asNumber();
    if (value >= 1 && value <= 65535) state.port = static_cast<int>(value);
  }
  state.installedAt =
      root["installedAt"].isString() ? root["installedAt"].asString() : "";
  state.bundleVersion =
      root["bundleVersion"].isString() ? root["bundleVersion"].asString() : "";
  state.lastAction =
      root["lastAction"].isString() ? root["lastAction"].asString() : "";
  out = state;
  return true;
}

bool readState(InstallState& out) {
  const std::string path = stateFilePath();
  if (path.empty() || !mes::platform::fileExists(path)) return false;
  std::string content;
  if (!mes::platform::readFile(path, content)) return false;
  InstallState state;
  if (!installStateFromJson(content, state)) return false;
  out = state;
  return true;
}

bool writeState(const InstallState& state) {
  const std::string path = stateFilePath();
  if (path.empty()) return false;
  const std::string parent = mes::platform::parentDir(path);
  if (!parent.empty() && !mes::platform::dirExists(parent))
    mes::platform::ensureDir(parent);
  return mes::platform::writeFile(path, installStateToJson(state));
}

bool isInstalled() {
  InstallState state;
  if (!readState(state)) return false;
  if (state.installDir.empty()) return false;
  if (!mes::platform::dirExists(state.installDir)) return false;
  // 标准布局有 app/；开发目录模式的应用入口直接位于安装目录下。
  if (mes::platform::dirExists(
          mes::platform::joinPath(state.installDir, "app")))
    return true;
  return mes::platform::fileExists(
      mes::platform::joinPath(state.installDir, "server/index.mjs"));
}

}  // namespace mes::core
