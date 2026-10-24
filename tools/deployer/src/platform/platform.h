// 校园作业分析 · 桌面部署器 —— 平台抽象层
// 仅在此层出现 OS API；核心引擎与 UI 保持平台无关。
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace mes::platform {

// ------------------------------------------------------------------ 基础信息
std::string platformName();   // windows / linux / macos
std::string architecture();   // x64 / x86 / arm64
std::string osVersionLabel(); // 展示用
std::string executablePath();
std::string currentDir();
std::string homeDir();
std::string tempDir();
bool ensureDir(const std::string& path);
bool removeTree(const std::string& path);
bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
bool writeFile(const std::string& path, const std::string& content);
bool readFile(const std::string& path, std::string& out);
uint64_t fileSize(const std::string& path);
bool copyTree(const std::string& from, const std::string& to, std::string& error);
bool makeExecutable(const std::string& path);
bool killProcessTree(uint64_t pid);
std::string joinPath(const std::string& a, const std::string& b);
std::string parentDir(const std::string& path);
std::string baseName(const std::string& path);
std::string randomHex(size_t bytes);
std::string envVar(const std::string& name);
bool isRunningAsAdmin();

// 系统字体候选（优先 CJK）。index 用于 TTC 集合。
struct FontCandidate {
  std::string path;
  int index = 0;
};
std::vector<FontCandidate> fontCandidates();
std::string preferredUiFont();

// ------------------------------------------------------------------ 进程管理
class Process {
 public:
  virtual ~Process() = default;
  virtual bool running() const = 0;
  virtual int exitCode() const = 0;
  virtual void stop() = 0;
  virtual uint64_t pid() const = 0;
};

using EnvPair = std::pair<std::string, std::string>;
std::unique_ptr<Process> spawnProcess(const std::vector<std::string>& args,
                                      const std::string& workDir,
                                      const std::vector<EnvPair>& env,
                                      const std::string& logFile,
                                      std::string& error,
                                      bool detached = false);
bool runCommand(const std::vector<std::string>& args,
                const std::string& workDir, std::string& output,
                std::string& error, int timeoutMs = 30000);
// 以管理员/root 权限重新执行本程序的一个子命令；失败时给出中文错误。
bool runElevated(const std::vector<std::string>& args, std::string& error);
bool openBrowser(const std::string& url);
void revealPath(const std::string& path);

// ------------------------------------------------------------------ HTTP
struct HttpResult {
  bool ok = false;
  int status = 0;
  std::string error;
  uint64_t bytes = 0;
};
using DownloadProgress =
    std::function<void(uint64_t received, uint64_t total)>;
HttpResult httpDownload(const std::string& url, const std::string& destPath,
                        DownloadProgress progress, std::atomic<bool>& cancel);
HttpResult httpGet(const std::string& url, std::string& body,
                   std::atomic<bool>& cancel);

// ------------------------------------------------------------------ 窗口/毛玻璃
// 在 glfwCreateWindow 之前调用，开启透明 framebuffer 等提示。
void configureWindowHints();
// 创建窗口后调用：应用系统级毛玻璃/圆角/暗色标题栏等增强（平台不支持则静默降级）。
void styleWindow(GLFWwindow* window);
void setWindowIconIfAvailable(GLFWwindow* window);
// 固定尺寸无边框窗口的自定义拖动（由 UI 调用，像素坐标）。
void beginWindowDrag(GLFWwindow* window);
void updateWindowDrag(GLFWwindow* window);

}  // namespace mes::platform
