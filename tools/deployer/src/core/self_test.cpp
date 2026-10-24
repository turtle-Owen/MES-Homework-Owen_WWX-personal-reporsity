// 校园作业分析 · 桌面部署器 —— 核心自检（--self-test / CTest）
// 覆盖：SHA-256 标准向量、.env 生成、服务文本、清单解析、ZIP 往返、
//       选项校验、状态序列化、提权参数编解码。
#include "core/core.h"
#include "core/core_util.h"
#include "core/elevated.h"
#include "core/json.h"
#include "core/manifest_parse.h"
#include "platform/platform.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mes::core {
namespace {

struct SelfTestContext {
  bool verbose = false;
  int passed = 0;
  int failed = 0;
  int skipped = 0;

  void pass(const std::string& name) {
    ++passed;
    if (verbose) std::printf("  [通过] %s\n", name.c_str());
  }

  void fail(const std::string& name, const std::string& detail) {
    ++failed;
    std::printf("  [失败] %s：%s\n", name.c_str(), detail.c_str());
  }

  void skip(const std::string& name, const std::string& detail) {
    ++skipped;
    if (verbose) std::printf("  [跳过] %s（%s）\n", name.c_str(), detail.c_str());
  }

  bool check(bool condition, const std::string& name,
             const std::string& detail = std::string()) {
    if (condition) {
      pass(name);
      return true;
    }
    fail(name, detail.empty() ? "断言不成立" : detail);
    return false;
  }

  bool checkEqual(const std::string& actual, const std::string& expected,
                  const std::string& name) {
    if (actual == expected) {
      pass(name);
      return true;
    }
    fail(name, "期望 " + expected + "，实际 " + actual);
    return false;
  }

  void section(const std::string& title) {
    if (verbose) std::printf("\n== %s ==\n", title.c_str());
  }
};

bool looksLikeHex(const std::string& text, size_t expectedLength) {
  if (text.size() != expectedLength) return false;
  for (char ch : text) {
    const bool digit = ch >= '0' && ch <= '9';
    const bool lower = ch >= 'a' && ch <= 'f';
    const bool upper = ch >= 'A' && ch <= 'F';
    if (!digit && !lower && !upper) return false;
  }
  return true;
}

// 平台层文件 IO 是否可用（stub 阶段返回 false，则跳过依赖文件系统的用例）。
bool platformFileIoAvailable() {
  const std::string base = mes::platform::tempDir();
  if (base.empty()) return false;
  const std::string probe =
      mes::platform::joinPath(base, "mes-selftest-probe-" + mes::platform::randomHex(4));
  if (!mes::platform::ensureDir(probe)) return false;
  const bool wrote = mes::platform::writeFile(
      mes::platform::joinPath(probe, "probe.txt"), "ok");
  std::string content;
  const bool read = mes::platform::readFile(
      mes::platform::joinPath(probe, "probe.txt"), content);
  mes::platform::removeTree(probe);
  return wrote && read && content == "ok";
}

void testSha256(SelfTestContext& context) {
  context.section("SHA-256");
  context.checkEqual(sha256Hex("", 0),
                     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                     "空字符串向量");
  const std::string abc = "abc";
  context.checkEqual(sha256Hex(abc.data(), abc.size()),
                     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                     "abc 向量");
  const std::string quick =
      "The quick brown fox jumps over the lazy dog";
  context.checkEqual(sha256Hex(quick.data(), quick.size()),
                     "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
                     "长句向量");
  // 一百万个 'a'（经典向量）分段喂入，验证流式实现。
  const std::string chunk(1000, 'a');
  std::string streamed;
  {
    // 复用 sha256Hex 的增量能力：直接拼成一整块（1 MB 内存可接受）。
    std::string million;
    million.reserve(1000000);
    for (int i = 0; i < 1000; ++i) million += chunk;
    streamed = sha256Hex(million.data(), million.size());
  }
  context.checkEqual(streamed,
                     "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                     "一百万个 a 向量");
  // 含 NUL 的二进制数据。
  const char binary[] = {'a', '\0', 'b'};
  context.check(looksLikeHex(sha256Hex(binary, sizeof(binary)), 64),
                "二进制数据（含 NUL）");
}

void testEnvFile(SelfTestContext& context) {
  context.section("配置 / 环境文件");
  Options options;
  options.edition = Edition::Server;
  options.port = 8787;
  options.lan = true;
  options.production = true;
  options.installDir = "C:\\Users\\John Doe\\mes-homework-server";
  options.dataDir = "C:\\Users\\John Doe\\mes data\\data";
  const std::string secret(96, 'a');
  const std::string content = envFileContent(options, secret);
  context.check(content.find("APP_SECRET=" + secret + "\n") != std::string::npos,
                "包含 APP_SECRET");
  context.check(content.find("PORT=8787\n") != std::string::npos, "包含 PORT");
  context.check(content.find("HOST=0.0.0.0\n") != std::string::npos,
                "服务器版 HOST=0.0.0.0");
  context.check(content.find("NODE_ENV=production\n") != std::string::npos,
                "生产模式 NODE_ENV=production");
  const std::string expectedDataDir = "DATA_DIR=\"C:/Users/John Doe/mes data/data\"";
  context.check(content.find(expectedDataDir) != std::string::npos,
                "含空格的 DATA_DIR 使用双引号且反斜杠转正斜杠（Node 可直接读取）",
                "未找到：" + expectedDataDir);
  // 路径里的 \n 序列不能以原始反斜杠形式出现（Node 会当成换行转义）。
  options.dataDir = "C:\\Users\\nancy\\data";
  const std::string content2 = envFileContent(options, secret);
  context.check(content2.find("DATA_DIR=\"C:/Users/nancy/data\"") !=
                    std::string::npos,
                "用户名含 \\n 序列的路径安全", content2);
  // 本地版默认值。
  Options local;
  local.port = 9000;
  const std::string localContent = envFileContent(local, secret);
  context.check(localContent.find("HOST=127.0.0.1\n") != std::string::npos,
                "本地版 HOST=127.0.0.1");
  context.check(localContent.find("NODE_ENV=development\n") != std::string::npos,
                "默认 development 模式");
  context.check(localContent.find("DATA_DIR=") != std::string::npos,
                "本地版包含 DATA_DIR");
}

void testServiceTexts(SelfTestContext& context) {
  context.section("启动脚本 / 系统服务文本");
  Options options;
  options.edition = Edition::Server;
  options.port = 8787;
  options.lan = true;
  options.autostart = true;
  options.installDir = "C:\\Users\\John Doe\\mes-homework-server";
  options.dataDir = "C:\\Users\\John Doe\\mes-homework-server\\data";

  const std::string cmd = windowsStartScript(options);
  context.check(cmd.find("@echo off") == 0, "start.cmd 以 @echo off 开头");
  context.check(cmd.find("set \"PORT=8787\"") != std::string::npos,
                "start.cmd 设置 PORT");
  context.check(cmd.find("cd /d \"%INSTALL%app\"") != std::string::npos,
                "start.cmd cd /d 到 app 目录");
  context.check(cmd.find("runtime\\node.exe") != std::string::npos,
                "start.cmd 使用自带 Node 运行时");
  context.check(cmd.find("app\\server\\index.mjs") != std::string::npos,
                "start.cmd 启动 app/server/index.mjs");
  context.check(cmd.find(">> \"%INSTALL%logs\\server.log\" 2>&1") !=
                    std::string::npos,
                "start.cmd 日志重定向");
  context.check(cmd.find("--env-file=\"%INSTALL%.env\"") != std::string::npos,
                "start.cmd 通过 --env-file 读取 .env");

  const std::string task = windowsServerTaskCommand(options);
  context.check(task.find("schtasks /Create") != std::string::npos,
                "schtasks 创建命令");
  context.check(task.find("/TN \"MES-Homework\"") != std::string::npos,
                "计划任务名 MES-Homework");
  context.check(task.find("/SC ONSTART") != std::string::npos,
                "开机触发 /SC ONSTART");
  context.check(task.find("/RU SYSTEM") != std::string::npos,
                "以 SYSTEM 运行");
  context.check(task.find("start.cmd") != std::string::npos,
                "任务启动 start.cmd");

  const std::string taskXml = windowsTaskXml(options);
  context.check(taskXml.find("<BootTrigger>") != std::string::npos,
                "任务 XML 含开机触发器");
  context.check(taskXml.find("<ExecutionTimeLimit>PT0S") != std::string::npos,
                "任务 XML 不限制运行时长");
  context.check(taskXml.find("&quot;") != std::string::npos,
                "任务 XML 对路径引号做转义");
  const std::string utf16 = utf16LeWithBom(taskXml);
  context.check(utf16.size() >= 2 &&
                    static_cast<unsigned char>(utf16[0]) == 0xFF &&
                    static_cast<unsigned char>(utf16[1]) == 0xFE,
                "任务 XML 转为带 BOM 的 UTF-16LE");
  context.check(utf16.size() > 2 && utf16.size() % 2 == 0,
                "UTF-16LE 字节数为偶数");

  const std::string unit = systemdUnit(options, "/opt/mes/runtime/node");
  context.check(unit.find("WorkingDirectory=") != std::string::npos,
                "systemd 单元含 WorkingDirectory");
  context.check(unit.find("Environment=") != std::string::npos,
                "systemd 单元含 Environment");
  context.check(unit.find("ExecStart=") != std::string::npos,
                "systemd 单元含 ExecStart");
  context.check(unit.find("Restart=on-failure") != std::string::npos,
                "systemd 单元含 Restart");
  context.check(unit.find("EnvironmentFile=") != std::string::npos,
                "systemd 单元通过 EnvironmentFile 注入密钥");
  context.check(unit.find("app/server/index.mjs") != std::string::npos,
                "systemd ExecStart 指向应用入口");

  const std::string plist =
      launchdPlist(options, "/opt/mes/runtime/node", "MES-Homework");
  context.check(plist.find("<key>ProgramArguments</key>") != std::string::npos,
                "launchd plist 含 ProgramArguments");
  context.check(plist.find("<key>EnvironmentVariables</key>") != std::string::npos,
                "launchd plist 含 EnvironmentVariables");
  context.check(plist.find("<key>RunAtLoad</key>") != std::string::npos,
                "launchd plist 含 RunAtLoad");
  context.check(plist.find("<key>KeepAlive</key>") != std::string::npos,
                "launchd plist 含 KeepAlive");
  context.check(plist.find("server/index.mjs") != std::string::npos,
                "launchd plist 指向应用入口");

  context.checkEqual(healthUrl(options),
                     "http://127.0.0.1:8787/api/health", "健康检查地址");
  context.check(!serviceLabel().empty(), "服务标签非空");
}

void testManifest(SelfTestContext& context) {
  context.section("Release 清单");
  const std::string json = R"({
    "version": "v0.2.1-rc3",
    "createdAt": "2026-10-24T12:00:00Z",
    "build": "26102403",
    "entries": [
      {"platform": "windows", "arch": "x64",
       "asset": "mes-homework-app-windows-x64.zip",
       "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
       "size": 123456789, "nodeVersion": "v24.0.0"},
      {"platform": "linux", "arch": "x64",
       "asset": "mes-homework-app-linux-x64.zip",
       "sha256": "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
       "size": 1}
    ]
  })";
  Manifest manifest;
  std::string error;
  const bool ok = parseManifest(json, manifest, error);
  context.check(ok, "解析合法清单", error);
  context.checkEqual(manifest.version, "v0.2.1-rc3", "清单版本");
  context.checkEqual(manifest.createdAt, "2026-10-24T12:00:00Z", "清单时间");
  context.check(manifest.entries.size() == 2, "清单条目数量");
  if (manifest.entries.size() == 2) {
    context.check(manifest.entries[0].size == 123456789, "条目 size");
    context.checkEqual(manifest.entries[0].nodeVersion, "v24.0.0",
                       "条目 nodeVersion");
  }
  PlatformInfo windows;
  windows.os = "windows";
  windows.arch = "x64";
  const ManifestEntry* entry = findManifestEntry(manifest, windows);
  context.check(entry != nullptr, "按平台查找条目");
  if (entry != nullptr) {
    context.checkEqual(manifestEntryUrl("v0.2.1-rc3", *entry),
                       "https://github.com/turtlelnc/MES-Homework/releases/"
                       "download/v0.2.1-rc3/mes-homework-app-windows-x64.zip",
                       "清单条目下载地址");
  }
  PlatformInfo x86;
  x86.os = "windows";
  x86.arch = "x86";
  context.check(findManifestEntry(manifest, x86) == nullptr,
                "找不到 x86 条目返回空");

  Manifest broken;
  std::string brokenError;
  context.check(!parseManifest("{not json", broken, brokenError),
                "拒绝非法 JSON");
  context.check(brokenError.find("安装清单") != std::string::npos,
                "非法 JSON 给出中文错误", brokenError);
  context.check(!parseManifest("{\"version\":\"x\",\"entries\":[]}", broken,
                               brokenError),
                "拒绝空 entries");
  context.check(!parseManifest(
                    "{\"version\":\"x\",\"entries\":[{\"platform\":\"windows\","
                    "\"arch\":\"x64\",\"asset\":\"a.zip\",\"sha256\":\"zz\"}]}",
                    broken, brokenError),
                "拒绝非法 sha256");

  context.checkEqual(manifestAssetName(), "mes-homework-app-manifest.json",
                     "清单资产名");
  PlatformInfo local = detectPlatform();
  context.checkEqual(appAssetName(local),
                     "mes-homework-app-" + local.os + "-" + local.arch + ".zip",
                     "应用包资产名");
  context.check(!deployerAssetName(local).empty(), "部署器资产名非空");
}

// 用 miniz 在临时目录写一个 zip，再用 extractZip 解压校验（真实往返）。
void testZipRoundTrip(SelfTestContext& context) {
  context.section("ZIP 解压");
  if (!platformFileIoAvailable()) {
    context.skip("ZIP 往返", "平台层文件 IO 暂不可用（stub 阶段）");
    return;
  }
  const std::string base =
      mes::platform::joinPath(mes::platform::tempDir(),
                              "mes-selftest-zip-" + mes::platform::randomHex(4));
  const std::string zipPath = mes::platform::joinPath(base, "payload.zip");
  const std::string dest = mes::platform::joinPath(base, "out");
  if (!mes::platform::ensureDir(base)) {
    context.skip("ZIP 往返", "无法创建临时目录");
    return;
  }
  struct Cleanup {
    std::string path;
    ~Cleanup() { mes::platform::removeTree(path); }
  } cleanup{base};

  mz_zip_archive writer;
  std::memset(&writer, 0, sizeof(writer));
  if (!mz_zip_writer_init_file(&writer, zipPath.c_str(), 0)) {
    context.skip("ZIP 往返", "miniz 无法创建测试 zip");
    return;
  }
  const std::string entryContent = "console.log('mes');\n";
  const std::string runtimeContent = "fake-node-binary";
  const std::string packageContent = "{\"name\":\"x\"}";
  const bool added =
      mz_zip_writer_add_mem(&writer, "app/server/index.mjs",
                            entryContent.data(), entryContent.size(),
                            MZ_DEFAULT_COMPRESSION) &&
      mz_zip_writer_add_mem(&writer, "app/package.json",
                            packageContent.data(), packageContent.size(),
                            MZ_DEFAULT_COMPRESSION) &&
      mz_zip_writer_add_mem(&writer, "runtime/node.exe",
                            runtimeContent.data(), runtimeContent.size(),
                            MZ_DEFAULT_COMPRESSION);
  const bool finalized = mz_zip_writer_finalize_archive(&writer) != 0;
  mz_zip_writer_end(&writer);
  if (!added || !finalized) {
    context.skip("ZIP 往返", "miniz 写入测试 zip 失败");
    return;
  }

  std::string error;
  const bool extracted = extractZip(zipPath, dest, error);
  context.check(extracted, "解压并保留相对目录结构", error);
  if (!extracted) return;
  std::string readBack;
  context.check(
      mes::platform::readFile(mes::platform::joinPath(dest, "app/server/index.mjs"),
                              readBack) &&
          readBack == entryContent,
      "解压出的 app/server/index.mjs 内容一致");
  context.check(
      mes::platform::readFile(mes::platform::joinPath(dest, "runtime/node.exe"),
                              readBack) &&
          readBack == runtimeContent,
      "解压出的 runtime/node.exe 内容一致");

  // zip-slip：带 ../ 的条目必须被拒绝。
  const std::string evilZip = mes::platform::joinPath(base, "evil.zip");
  mz_zip_archive evilWriter;
  std::memset(&evilWriter, 0, sizeof(evilWriter));
  if (mz_zip_writer_init_file(&evilWriter, evilZip.c_str(), 0)) {
    mz_zip_writer_add_mem(&evilWriter, "../escape.txt", "x", 1,
                          MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&evilWriter);
    mz_zip_writer_end(&evilWriter);
    std::string evilError;
    const bool evilExtracted = extractZip(
        evilZip, mes::platform::joinPath(base, "evil-out"), evilError);
    context.check(!evilExtracted, "拒绝 zip-slip 越界条目");
    context.check(evilError.find("越界") != std::string::npos ||
                      evilError.find("拒绝") != std::string::npos,
                  "zip-slip 错误文案为中文", evilError);
  }
}

void testOptions(SelfTestContext& context) {
  context.section("安装选项校验");
  const std::string tempBase = mes::platform::tempDir();
  Options valid;
  valid.installDir = mes::platform::joinPath(tempBase, "mes-options-install");
  valid.port = 8787;
  valid.embedDemoPayload = true;
  context.checkEqual(validateOptions(valid), "", "合法选项返回空错误");

  Options badPort;
  badPort.port = 0;
  badPort.installDir = valid.installDir;
  badPort.embedDemoPayload = true;
  context.check(!validateOptions(badPort).empty(), "拒绝非法端口");

  Options relative;
  relative.installDir = "relative/path";
  relative.embedDemoPayload = true;
  context.check(!validateOptions(relative).empty(), "拒绝相对安装目录");

  Options sameDir;
  sameDir.installDir = valid.installDir;
  sameDir.dataDir = valid.installDir;
  sameDir.embedDemoPayload = true;
  context.check(!validateOptions(sameDir).empty(), "拒绝数据目录与安装目录相同");

  Options both;
  both.installDir = valid.installDir;
  both.payloadDir = tempBase;
  both.payloadZip = mes::platform::joinPath(tempBase, "x.zip");
  both.embedDemoPayload = true;
  context.check(!validateOptions(both).empty(), "拒绝同时指定本地包与本地目录");

  Options missingZip;
  missingZip.installDir = valid.installDir;
  missingZip.payloadZip = mes::platform::joinPath(tempBase, "not-exists-xyz.zip");
  context.check(!validateOptions(missingZip).empty(), "拒绝不存在的本地安装包");
}

void testStateSerialization(SelfTestContext& context) {
  context.section("安装状态");
  InstallState state;
  state.version = "v0.2.1-rc3";
  state.edition = "server";
  state.installDir = "C:\\Users\\John \"Doe\"\\mes-homework";
  state.dataDir = "C:\\Users\\John \"Doe\"\\mes-homework\\data";
  state.port = 8787;
  state.installedAt = "2026-10-24T12:00:00Z";
  state.bundleVersion = "v0.2.1-rc3";
  state.lastAction = "install";
  const std::string json = installStateToJson(state);
  context.check(json.find("\\\"Doe\\\"") != std::string::npos,
                "状态 JSON 安全转义引号");
  InstallState parsed;
  context.check(installStateFromJson(json, parsed), "状态 JSON 可解析");
  context.checkEqual(parsed.installDir, state.installDir, "状态安装目录往返");
  context.checkEqual(parsed.edition, state.edition, "状态版本类型往返");
  context.check(parsed.port == 8787, "状态端口往返");
  context.checkEqual(parsed.lastAction, "install", "状态动作往返");
  InstallState broken;
  context.check(!installStateFromJson("{broken", broken), "拒绝损坏的状态文件");
  context.check(!isoNow().empty(), "isoNow 非空");
  context.check(stateFilePath().find(".mes-homework-deployer.json") !=
                    std::string::npos,
                "状态文件路径");
}

void testElevatedProtocol(SelfTestContext& context) {
  context.section("提权参数协议");
  Options options;
  options.edition = Edition::Server;
  options.installDir = "C:\\ProgramData\\MES Homework";
  options.dataDir = "C:\\ProgramData\\MES Homework\\data";
  options.port = 9001;
  options.lan = true;
  options.autostart = true;
  options.firewall = true;
  options.production = true;
  const std::vector<std::string> args =
      elevatedActionArgs("service-install", options);
  std::string action;
  Options parsed;
  std::string error;
  context.check(parseElevatedActionArgs(args, action, parsed, error),
                "提权参数解析", error);
  context.checkEqual(action, "service-install", "提权动作名往返");
  context.checkEqual(parsed.installDir, options.installDir, "提权安装目录往返");
  context.check(parsed.port == 9001, "提权端口往返");
  context.check(parsed.edition == Edition::Server, "提权版本类型往返");
  context.check(parsed.autostart && parsed.firewall && parsed.lan,
                "提权布尔开关往返");
  context.check(!parseElevatedActionArgs({"--elevated-action=service-install"},
                                         action, parsed, error),
                "缺少安装目录时拒绝");
  context.check(!parseElevatedActionArgs({"--elevated-action=hack"}, action,
                                         parsed, error),
                "拒绝未知动作");
}

void testPlatformInfo(SelfTestContext& context) {
  context.section("平台检测");
  const PlatformInfo info = detectPlatform();
  context.checkEqual(info.os, currentOsSlug(), "平台 os 与编译期一致");
  context.checkEqual(info.arch, currentArchSlug(), "平台 arch 与编译期一致");
  context.check(!info.osLabel.empty(), "平台展示名非空");
  context.check(!info.archLabel.empty(), "架构展示名非空");
  context.check(info.appRuntimeSupported == (info.arch != "x86"),
                "32 位标记为不支持自带运行时");
  context.check(!info.runtimeNote.empty(), "平台说明非空");
  context.checkEqual(std::string(editionName(Edition::Local)), "本地版",
                     "本地版名称");
  context.checkEqual(std::string(editionName(Edition::Server)), "服务器版",
                     "服务器版名称");
  const std::string secret = generateSecret();
  context.check(looksLikeHex(secret, 96) || secret.empty(),
                "随机密钥为 96 位十六进制（或平台随机数不可用）", secret);
}

}  // namespace

bool runSelfTest(bool verbose) {
  SelfTestContext context;
  context.verbose = verbose;
  if (verbose) {
    std::printf("校园作业分析 · 桌面部署器核心自检（%s / build %s）\n", kVersion,
                kBuild);
  }
  testSha256(context);
  testEnvFile(context);
  testServiceTexts(context);
  testManifest(context);
  testZipRoundTrip(context);
  testOptions(context);
  testStateSerialization(context);
  testElevatedProtocol(context);
  testPlatformInfo(context);
  if (verbose) {
    std::printf("\n自检完成：通过 %d，失败 %d，跳过 %d\n", context.passed,
                context.failed, context.skipped);
  }
  return context.failed == 0;
}

}  // namespace mes::core
