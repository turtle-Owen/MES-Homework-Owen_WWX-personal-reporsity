// 校园作业分析 · 桌面部署器 —— 管理员/root 提权子命令协议
//
// 服务器版安装系统服务、放行防火墙端口需要管理员权限。平台层提供
// platform::runElevated(args)：以管理员权限重新执行本程序并追加 args。
// main.cpp 检测到 --elevated-action=... 时，把全部参数交给
// runElevatedAction() 执行单个系统级动作（不创建窗口）。
//
// 该头文件不属于对外契约（core.h），由 main.cpp / engine.cpp 共用。
#pragma once

#include "core/core.h"

#include <string>
#include <vector>

namespace mes::core {

// 把一次提权动作编码成命令行参数（单 token 形式，便于原样转发）。
std::vector<std::string> elevatedActionArgs(const std::string& action,
                                            const Options& options);

// 解析 main.cpp 收到的参数；成功返回 true 并填充 action / options。
// 失败时 error 为中文原因。
bool parseElevatedActionArgs(const std::vector<std::string>& args,
                             std::string& action, Options& options,
                             std::string& error);

// 支持的提权动作名。
bool isSupportedElevatedAction(const std::string& action);

// 在提权进程中执行单个动作，返回进程退出码（0 = 成功）。
int runElevatedAction(const std::vector<std::string>& args);

}  // namespace mes::core
