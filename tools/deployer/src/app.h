// 应用状态机：欢迎页 → 配置页 → 安装进度 → 结果页；负责与核心引擎工作线程交互。
#pragma once

#include "core/core.h"
#include "ui/screens.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mes {

enum class Screen { Welcome, Configure, Installing, Result, About };

struct LogLine {
  std::string level;
  std::string text;
};

struct AppState {
  core::PlatformInfo platform;
  Screen screen = Screen::Welcome;
  Screen previousScreen = Screen::Welcome;
  core::Edition edition = core::Edition::Local;
  core::Options options;

  // 已安装实例
  bool installed = false;
  core::InstallState installState;
  bool serviceRunning = false;

  // 后台安装任务
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::mutex mutex;
  core::ProgressState progress;
  std::deque<LogLine> logs;
  core::DeploymentResult result;
  bool resultReady = false;
  bool busy = false;

  // 跨线程标记：工作线程写，渲染线程在 appFrame 消费。
  bool logsDirty = false;
  bool progressDirty = false;
  bool hasPendingBanner = false;
  bool pendingBannerError = false;
  std::string pendingBannerText;
  bool actionRefresh = false;
  bool postUninstall = false;

  // 渲染线程使用的快照（appFrame 内加锁拷贝）。
  core::ProgressState uiProgress;
  std::deque<LogLine> uiLogs;
  core::DeploymentResult uiResult;
  bool uiResultReady = false;
  bool previewData = false;  // 演示模式预置数据（仅用于界面验收）

  // 短任务线程（停止/卸载/打开）。
  std::thread actionWorker;
  std::atomic<bool> actionBusy{false};

  // UI 瞬时状态
  std::string bannerText;
  bool bannerError = false;
  double bannerAt = 0.0;
  bool showLogs = false;
  bool confirmUninstall = false;  // 卸载二次确认弹层
  bool uninstallRemoveData = false;
  bool showCancelConfirm = false;  // 取消部署确认弹层
  bool cancelRequested = false;
  bool demoMode = false;
  double startedAt = 0.0;
  float spinnerPhase = 0.0f;
  double now = 0.0;
  float logScroll = 0.0f;
  bool logAutoScroll = true;
  float aboutScroll = 0.0f;

  std::string lastDirPicked;
};

void appInit(AppState& state, bool demoMode);
void appFrame(AppState& state);
void appShutdown(AppState& state);

// 供 UI 调用的动作（内部启动工作线程）。
void startDeployment(AppState& state);
void startStop(AppState& state);
void startUninstall(AppState& state);
void startOpenApp(AppState& state);
void refreshInstalledState(AppState& state);
void notify(AppState& state, const std::string& text, bool error);
void resetToWelcome(AppState& state);

// 模态弹层是否打开（此时页面控件不接收输入）。
bool modalOpen(const AppState& state);
// 是否正在执行后台任务。
bool deploymentPending(const AppState& state);
// 当前有效的安装选项（已安装时以安装状态为准）。
core::Options effectiveOptions(const AppState& state);
// 展示/写入前统一本地路径分隔符（Windows 用反斜杠，其余平台原样返回）。
std::string normalizePath(const std::string& path);

}  // namespace mes
