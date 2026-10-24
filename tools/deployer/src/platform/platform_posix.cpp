// 校园作业分析 · 桌面部署器 —— POSIX 通用平台能力（Linux / macOS）。
//
// 本文件同时被主目标与 mes-deployer-test 编译，因此不引用 GLFW：
// 字体候选/窗口增强在 platform_linux.cpp / platform_mac.mm / window_posix.cpp。
#include "platform/platform.h"

#if !defined(_WIN32)

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>  // realpath / arc4random_buf
#include <sys/sysctl.h>
#endif
#if defined(__linux__) && defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define MES_HAVE_GETRANDOM 1
#endif
#endif

extern char** environ;

namespace mes::platform {
namespace {

constexpr size_t kMaxCapture = 2u << 20;  // 命令输出最多保留 2MB

std::string errnoText(int code) {
  const char* text = std::strerror(code);
  return text ? std::string(text) : std::string("错误码 ") + std::to_string(code);
}

std::string trimTrailingSlashes(std::string path) {
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}

std::string parentDirOnly(const std::string& path) {
  if (path.empty()) return {};
  const std::string trimmed = trimTrailingSlashes(path);
  const size_t slash = trimmed.find_last_of('/');
  if (slash == std::string::npos) return {};
  if (slash == 0) return "/";
  return trimmed.substr(0, slash);
}

std::string absolutePath(const std::string& path) {
  if (path.empty()) return {};
  if (path.front() == '/') return path;
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) return path;
  return std::string(cwd) + "/" + path;
}

// 词法规整：处理 "." / ".." / 重复斜杠（不解析符号链接）。
std::string lexicalNormalize(const std::string& path) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t slash = path.find('/', start);
    std::string part = slash == std::string::npos
                           ? path.substr(start)
                           : path.substr(start, slash - start);
    if (part == "..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  std::string result;
  for (const std::string& part : parts) {
    result += "/";
    result += part;
  }
  return result.empty() ? "/" : result;
}

bool pathIsWithin(const std::string& child, const std::string& parent) {
  if (parent == "/") return child != "/";
  return child.compare(0, parent.size(), parent) == 0 &&
         child.size() > parent.size() && child[parent.size()] == '/';
}

bool ensureDirRecursive(const std::string& path, mode_t mode) {
  if (path.empty()) return false;
  const std::string absolute = trimTrailingSlashes(absolutePath(path));
  if (absolute.empty() || absolute == "/") return true;
  std::string current;
  size_t start = 1;  // 跳过开头的 '/'
  while (start <= absolute.size()) {
    const size_t slash = absolute.find('/', start);
    const std::string part =
        slash == std::string::npos
            ? absolute.substr(start)
            : absolute.substr(start, slash - start);
    if (!part.empty()) {
      current += "/";
      current += part;
      if (mkdir(current.c_str(), mode) != 0 && errno != EEXIST) return false;
      struct stat info{};
      if (stat(current.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        return false;
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return true;
}

bool removeTreeImpl(const std::string& path, std::string& error) {
  struct stat info{};
  if (lstat(path.c_str(), &info) != 0) {
    if (errno == ENOENT) return true;  // 不存在视为成功（幂等）
    error = "删除失败：" + path + "（" + errnoText(errno) + "）";
    return false;
  }
  // 符号链接/普通文件：直接 unlink，绝不跟随进入目标。
  if (!S_ISDIR(info.st_mode)) {
    if (unlink(path.c_str()) != 0) {
      error = "删除文件失败：" + path + "（" + errnoText(errno) + "）";
      return false;
    }
    return true;
  }
  DIR* dir = opendir(path.c_str());
  if (!dir) {
    error = "读取目录失败：" + path + "（" + errnoText(errno) + "）";
    return false;
  }
  bool ok = true;
  while (dirent* entry = readdir(dir)) {
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0)
      continue;
    const std::string child = path + "/" + entry->d_name;
    if (!removeTreeImpl(child, error)) {
      ok = false;
      break;
    }
  }
  closedir(dir);
  if (!ok) return false;
  if (rmdir(path.c_str()) != 0 && errno != ENOENT) {
    error = "删除目录失败：" + path + "（" + errnoText(errno) + "）";
    return false;
  }
  return true;
}

bool writeFileImpl(const std::string& path, const std::string& content,
                   std::string* error) {
  const size_t slash = path.find_last_of('/');
  if (slash != std::string::npos && slash > 0) {
    const std::string parent = path.substr(0, slash);
    if (!ensureDirRecursive(parent, 0755)) {
      if (error) *error = "创建父目录失败：" + parent;
      return false;
    }
  }
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    if (error) *error = "写入文件失败：" + path + "（" + errnoText(errno) + "）";
    return false;
  }
  size_t offset = 0;
  bool ok = true;
  while (offset < content.size()) {
    const ssize_t written =
        write(fd, content.data() + offset, content.size() - offset);
    if (written <= 0) {
      if (errno == EINTR) continue;
      ok = false;
      break;
    }
    offset += static_cast<size_t>(written);
  }
  close(fd);
  if (!ok && error) *error = "写入文件失败：" + path + "（" + errnoText(errno) + "）";
  return ok;
}

bool readFileImpl(const std::string& path, std::string& out) {
  out.clear();
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  struct stat info{};
  if (fstat(fd, &info) == 0 && info.st_size > 0) {
    out.reserve(static_cast<size_t>(
        std::min<off_t>(info.st_size, static_cast<off_t>(64LL << 20))));
  }
  char buffer[65536];
  bool ok = true;
  for (;;) {
    const ssize_t got = read(fd, buffer, sizeof(buffer));
    if (got < 0) {
      if (errno == EINTR) continue;
      ok = false;
      break;
    }
    if (got == 0) break;
    out.append(buffer, static_cast<size_t>(got));
  }
  close(fd);
  if (!ok) out.clear();
  return ok;
}

bool copyFileContents(const std::string& from, const std::string& to,
                      std::string& error) {
  const int in = open(from.c_str(), O_RDONLY);
  if (in < 0) {
    error = "复制文件失败：" + from + "（" + errnoText(errno) + "）";
    return false;
  }
  const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    close(in);
    error = "创建目标文件失败：" + to + "（" + errnoText(errno) + "）";
    return false;
  }
  char buffer[65536];
  bool ok = true;
  for (;;) {
    const ssize_t got = read(in, buffer, sizeof(buffer));
    if (got < 0) {
      if (errno == EINTR) continue;
      ok = false;
      break;
    }
    if (got == 0) break;
    ssize_t offset = 0;
    while (offset < got) {
      const ssize_t written = write(out, buffer + offset,
                                    static_cast<size_t>(got - offset));
      if (written <= 0) {
        if (errno == EINTR) continue;
        ok = false;
        break;
      }
      offset += written;
    }
    if (!ok) break;
  }
  close(in);
  close(out);
  if (!ok) error = "复制文件失败：" + from + "（" + errnoText(errno) + "）";
  return ok;
}

bool copyTreeImpl(const std::string& from, const std::string& to,
                  std::string& error) {
  struct stat info{};
  if (lstat(from.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
    error = "复制失败：源目录不存在 " + from;
    return false;
  }
  const std::string fromNorm = lexicalNormalize(absolutePath(from));
  const std::string toNorm = lexicalNormalize(absolutePath(to));
  if (fromNorm == toNorm) {
    error = "复制失败：源目录与目标目录相同";
    return false;
  }
  if (pathIsWithin(toNorm, fromNorm)) {
    error = "复制失败：目标目录位于源目录内部（会造成递归复制）";
    return false;
  }
  if (pathIsWithin(fromNorm, toNorm)) {
    error = "复制失败：源目录位于目标目录内部";
    return false;
  }
  if (!ensureDirRecursive(toNorm, 0755)) {
    error = "复制失败：无法创建目标目录 " + to;
    return false;
  }
  DIR* dir = opendir(fromNorm.c_str());
  if (!dir) {
    error = "复制失败：无法读取源目录 " + from + "（" + errnoText(errno) + "）";
    return false;
  }
  bool ok = true;
  while (dirent* entry = readdir(dir)) {
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0)
      continue;
    const std::string src = fromNorm + "/" + entry->d_name;
    const std::string dst = toNorm + "/" + entry->d_name;
    struct stat childInfo{};
    if (lstat(src.c_str(), &childInfo) != 0) {
      error = "复制失败：无法读取 " + src + "（" + errnoText(errno) + "）";
      ok = false;
      break;
    }
    if (S_ISLNK(childInfo.st_mode)) {
      // 符号链接按链接本身复制，不跟随（避免越界）。
      char target[PATH_MAX];
      const ssize_t length = readlink(src.c_str(), target, sizeof(target) - 1);
      if (length < 0) {
        error = "复制符号链接失败：" + src + "（" + errnoText(errno) + "）";
        ok = false;
        break;
      }
      target[length] = '\0';
      unlink(dst.c_str());
      if (symlink(target, dst.c_str()) != 0) {
        error = "创建符号链接失败：" + dst + "（" + errnoText(errno) + "）";
        ok = false;
        break;
      }
    } else if (S_ISDIR(childInfo.st_mode)) {
      if (!copyTreeImpl(src, dst, error)) {
        ok = false;
        break;
      }
    } else if (S_ISREG(childInfo.st_mode)) {
      if (!copyFileContents(src, dst, error)) {
        ok = false;
        break;
      }
      chmod(dst.c_str(), childInfo.st_mode & 07777);
    }
    // 其它类型（套接字/设备等）不复制。
  }
  closedir(dir);
  return ok;
}

std::string environmentValue(const std::vector<std::string>& entries,
                             const std::string& key) {
  const std::string prefix = key + "=";
  for (const std::string& entry : entries)
    if (entry.compare(0, prefix.size(), prefix) == 0)
      return entry.substr(prefix.size());
  return {};
}

// 继承当前环境并应用覆盖项。
std::vector<std::string> mergedEnvironment(const std::vector<EnvPair>& env) {
  std::vector<std::string> entries;
  for (char** item = environ; item && *item; ++item) entries.emplace_back(*item);
  for (const EnvPair& pair : env) {
    if (pair.first.empty()) continue;
    const std::string entry = pair.first + "=" + pair.second;
    const std::string prefix = pair.first + "=";
    bool replaced = false;
    for (std::string& existing : entries) {
      if (existing.size() >= prefix.size() &&
          existing.compare(0, prefix.size(), prefix) == 0) {
        existing = entry;
        replaced = true;
        break;
      }
    }
    if (!replaced) entries.push_back(entry);
  }
  return entries;
}

std::vector<char*> toCharPtrs(std::vector<std::string>& storage) {
  std::vector<char*> pointers;
  pointers.reserve(storage.size() + 1);
  for (std::string& item : storage) pointers.push_back(&item[0]);
  pointers.push_back(nullptr);
  return pointers;
}

std::string searchExecutable(const std::string& name,
                             const std::string& pathEnv) {
  if (name.empty()) return {};
  if (name.find('/') != std::string::npos) return name;
  size_t start = 0;
  while (start <= pathEnv.size()) {
    const size_t colon = pathEnv.find(':', start);
    std::string dir = colon == std::string::npos
                          ? pathEnv.substr(start)
                          : pathEnv.substr(start, colon - start);
    if (dir.empty()) dir = ".";
    const std::string candidate = dir + "/" + name;
    if (access(candidate.c_str(), X_OK) == 0) return candidate;
    if (colon == std::string::npos) break;
    start = colon + 1;
  }
  return {};
}

// fork + execvp，带 exec 失败探测。成功返回子进程 pid，并填好 stdout 管道读端。
struct ChildSpawnResult {
  pid_t pid = -1;
  int stdoutFd = -1;
  int error = 0;  // exec / chdir 失败时的 errno
};

ChildSpawnResult spawnChild(const std::vector<std::string>& args,
                            const std::string& executable,
                            const std::string& workDir,
                            std::vector<std::string>& environment,
                            bool redirectOutput, bool detached,
                            int logFd) {
  ChildSpawnResult result;
  int outPipe[2] = {-1, -1};
  if (redirectOutput) {
    if (pipe(outPipe) != 0) {
      result.error = errno;
      return result;
    }
  }
  int errPipe[2] = {-1, -1};
  if (pipe(errPipe) != 0) {
    if (outPipe[0] >= 0) {
      close(outPipe[0]);
      close(outPipe[1]);
    }
    result.error = errno;
    return result;
  }
  fcntl(errPipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);

  std::vector<std::string> argStorage = args;
  std::vector<char*> argv = toCharPtrs(argStorage);
  std::vector<char*> envp = toCharPtrs(environment);

  const pid_t pid = fork();
  if (pid < 0) {
    result.error = errno;
    if (outPipe[0] >= 0) {
      close(outPipe[0]);
      close(outPipe[1]);
    }
    close(errPipe[0]);
    close(errPipe[1]);
    return result;
  }
  if (pid == 0) {
    // ---- 子进程：只调用 async-signal-safe 操作 ----
    close(errPipe[0]);
    if (detached) {
      setsid();
    } else {
      setpgid(0, 0);  // 便于按进程组 kill 整棵树
    }
    if (!workDir.empty()) {
      if (chdir(workDir.c_str()) != 0) {
        const int code = errno;
        (void)!write(errPipe[1], &code, sizeof(code));
        _exit(126);
      }
    }
    const int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0) {
      if (!redirectOutput && logFd < 0) {
        dup2(devNull, 1);
        dup2(devNull, 2);
      }
      dup2(devNull, 0);
      if (devNull > 2) close(devNull);
    }
    if (redirectOutput) {
      dup2(outPipe[1], 1);
      dup2(outPipe[1], 2);
      if (outPipe[1] > 2) close(outPipe[1]);
      if (outPipe[0] > 2) close(outPipe[0]);
    }
    if (logFd >= 0) {
      dup2(logFd, 1);
      dup2(logFd, 2);
      if (logFd > 2) close(logFd);
    }
    if (executable.empty() || executable.find('/') == std::string::npos)
      execvp(argv[0], argv.data());
    else
      execve(executable.c_str(), argv.data(), envp.data());
    const int code = errno;  // 执行到这里说明 exec 失败
    (void)!write(errPipe[1], &code, sizeof(code));
    _exit(127);
  }
  // ---- 父进程 ----
  if (outPipe[1] >= 0) close(outPipe[1]);
  close(errPipe[1]);
  int execError = 0;
  ssize_t got = read(errPipe[0], &execError, sizeof(execError));
  close(errPipe[0]);
  if (got == static_cast<ssize_t>(sizeof(execError)) && execError != 0) {
    int status = 0;
    waitpid(pid, &status, 0);
    if (outPipe[0] >= 0) close(outPipe[0]);
    result.error = execError;
    result.pid = -1;
    return result;
  }
  result.pid = pid;
  if (outPipe[0] >= 0) result.stdoutFd = outPipe[0];
  return result;
}

struct CaptureResult {
  bool ok = false;
  int exitCode = -1;
  int error = 0;   // 启动/执行失败 errno
  bool timedOut = false;
  std::string output;
};

CaptureResult runCapture(const std::vector<std::string>& args,
                         const std::string& workDir, int timeoutMs) {
  CaptureResult result;
  if (args.empty() || args[0].empty()) {
    result.error = EINVAL;
    return result;
  }
  std::vector<std::string> environment = mergedEnvironment({});
  const std::string pathEnv = environmentValue(environment, "PATH");
  const std::string executable = searchExecutable(args[0], pathEnv);
  if (executable.empty()) {
    result.error = ENOENT;
    return result;
  }
  ChildSpawnResult child =
      spawnChild(args, executable, workDir, environment, true, false, -1);
  if (child.pid < 0) {
    result.error = child.error;
    return result;
  }
  if (timeoutMs <= 0) timeoutMs = 30000;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
  int status = 0;
  bool reaped = false;
  bool outputClosed = false;
  char buffer[65536];
  for (;;) {
    if (!outputClosed) {
      struct pollfd descriptor{};
      descriptor.fd = child.stdoutFd;
      descriptor.events = POLLIN;
      const int polled = poll(&descriptor, 1, 50);
      if (polled > 0) {
        const ssize_t readSize = read(child.stdoutFd, buffer, sizeof(buffer));
        if (readSize > 0) {
          if (result.output.size() < kMaxCapture) {
            const size_t room = kMaxCapture - result.output.size();
            result.output.append(
                buffer, std::min<size_t>(static_cast<size_t>(readSize), room));
          }
          continue;  // 可能还有数据
        }
        if (readSize == 0) outputClosed = true;  // 子进程关闭了 stdout
      }
    } else {
      // 管道已 EOF 但子进程可能还在运行（例如后台孙进程持有管道）：
      // 用 sleep 代替 poll，避免 POLLHUP 导致的忙等空转。
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const pid_t reapedPid = waitpid(child.pid, &status, WNOHANG);
    if (reapedPid == child.pid) {
      reaped = true;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      result.timedOut = true;
      kill(-child.pid, SIGKILL);
      kill(child.pid, SIGKILL);
      waitpid(child.pid, &status, 0);
      reaped = true;
      break;
    }
  }
  // 排空剩余输出
  for (;;) {
    const ssize_t readSize = read(child.stdoutFd, buffer, sizeof(buffer));
    if (readSize > 0) {
      if (result.output.size() < kMaxCapture) {
        const size_t room = kMaxCapture - result.output.size();
        result.output.append(
            buffer, std::min<size_t>(static_cast<size_t>(readSize), room));
      }
      continue;
    }
    if (readSize < 0 && errno == EINTR) continue;
    break;
  }
  close(child.stdoutFd);
  if (!reaped) waitpid(child.pid, &status, 0);
  if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exitCode = 128 + WTERMSIG(status);
  } else {
    result.exitCode = -1;
  }
  result.ok = true;
  return result;
}

bool spawnDetachedChecked(const std::vector<std::string>& args) {
  if (args.empty() || args[0].empty()) return false;
  std::vector<std::string> environment = mergedEnvironment({});
  const std::string executable =
      searchExecutable(args[0], environmentValue(environment, "PATH"));
  if (executable.empty()) return false;
  ChildSpawnResult child = spawnChild(args, executable, "", environment, false,
                                      true, -1);
  return child.pid >= 0;
}

#if defined(__APPLE__)
std::string shellQuote(const std::string& arg) {
  std::string out = "'";
  for (char c : arg) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}
#endif

// ---------------------------------------------------------------- 进程类
class PosixProcess final : public Process {
 public:
  explicit PosixProcess(pid_t pid) : pid_(pid) {}

  bool running() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (exited_) return false;
    int status = 0;
    const pid_t reaped = waitpid(pid_, &status, WNOHANG);
    if (reaped == pid_) {
      finalizeLocked(status);
      return false;
    }
    if (reaped == 0) return true;
    // ECHILD：进程已被 init 接管或已回收，用信号 0 探测存活。
    if (kill(pid_, 0) == 0) return true;
    exited_ = true;
    exitCode_ = -1;
    return false;
  }

  int exitCode() const override {
    running();
    std::lock_guard<std::mutex> lock(mutex_);
    return exited_ ? exitCode_ : -1;
  }

  void stop() override {
    if (!running()) return;
    kill(-pid_, SIGTERM);
    kill(pid_, SIGTERM);
    for (int i = 0; i < 40; ++i) {  // 最多等 2 秒
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (!running()) return;
    }
    kill(-pid_, SIGKILL);
    kill(pid_, SIGKILL);
    for (int i = 0; i < 20; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (!running()) return;
    }
  }

  uint64_t pid() const override { return static_cast<uint64_t>(pid_); }

 private:
  void finalizeLocked(int status) const {
    if (WIFEXITED(status))
      exitCode_ = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      exitCode_ = 128 + WTERMSIG(status);
    else
      exitCode_ = -1;
    exited_ = true;
  }

  mutable std::mutex mutex_;
  pid_t pid_ = -1;
  mutable bool exited_ = false;
  mutable int exitCode_ = -1;
};

// ---------------------------------------------------------------- 随机数
bool fillRandomBytes(unsigned char* buffer, size_t size) {
  if (size == 0) return true;
#if defined(MES_HAVE_GETRANDOM)
  {
    size_t filled = 0;
    while (filled < size) {
      const ssize_t got =
          getrandom(buffer + filled, size - filled, 0);
      if (got < 0) {
        if (errno == EINTR) continue;
        break;  // 回退到 /dev/urandom
      }
      filled += static_cast<size_t>(got);
    }
    if (filled == size) return true;
  }
#elif defined(__APPLE__)
  arc4random_buf(buffer, size);
  return true;
#endif
  const int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return false;
  size_t offset = 0;
  bool ok = true;
  while (offset < size) {
    const ssize_t got = read(fd, buffer + offset, size - offset);
    if (got <= 0) {
      if (got < 0 && errno == EINTR) continue;
      ok = false;
      break;
    }
    offset += static_cast<size_t>(got);
  }
  close(fd);
  return ok;
}

}  // namespace

// ================================================================ 基础信息
std::string platformName() {
#if defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string architecture() {
  struct utsname info{};
  if (uname(&info) != 0) return "unknown";
  const std::string machine = info.machine;
  if (machine == "x86_64" || machine == "amd64" || machine == "x64")
    return "x64";
  if (machine == "i386" || machine == "i486" || machine == "i586" ||
      machine == "i686" || machine == "x86")
    return "x86";
  if (machine == "aarch64" || machine == "arm64") return "arm64";
  if (machine.rfind("armv7", 0) == 0 || machine.rfind("armv6", 0) == 0 ||
      machine.rfind("arm", 0) == 0)
    return "arm";
  return machine;
}

std::string osVersionLabel() {
#if defined(__APPLE__)
  char version[256] = {0};
  size_t length = sizeof(version);
  if (sysctlbyname("kern.osproductversion", version, &length, nullptr, 0) == 0 &&
      version[0] != '\0')
    return std::string("macOS ") + version;
#endif
#if defined(__linux__)
  std::string osRelease;
  if (readFileImpl("/etc/os-release", osRelease)) {
    const size_t pos = osRelease.find("PRETTY_NAME=");
    if (pos != std::string::npos) {
      size_t start = pos + std::strlen("PRETTY_NAME=");
      size_t end = osRelease.find('\n', start);
      std::string pretty = osRelease.substr(
          start, end == std::string::npos ? std::string::npos : end - start);
      if (!pretty.empty() && pretty.front() == '"' && pretty.back() == '"')
        pretty = pretty.substr(1, pretty.size() - 2);
      if (!pretty.empty()) return pretty;
    }
  }
#endif
  struct utsname info{};
  if (uname(&info) != 0) return "未知系统";
  std::string label = info.sysname;
  label += " ";
  label += info.release;
  return label;
}

std::string executablePath() {
#if defined(__linux__)
  std::vector<char> buffer(4096);
  for (int attempt = 0; attempt < 4; ++attempt) {
    const ssize_t length =
        readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length < 0) break;
    if (static_cast<size_t>(length) < buffer.size())
      return std::string(buffer.data(), static_cast<size_t>(length));
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = 4096;
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    char resolved[PATH_MAX];
    if (realpath(buffer.data(), resolved)) return resolved;
    return std::string(buffer.data());
  }
  buffer.resize(size);
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    char resolved[PATH_MAX];
    if (realpath(buffer.data(), resolved)) return resolved;
    return std::string(buffer.data());
  }
#endif
  return {};
}

std::string currentDir() {
  std::vector<char> buffer(4096);
  for (int attempt = 0; attempt < 4; ++attempt) {
    if (getcwd(buffer.data(), buffer.size())) return std::string(buffer.data());
    if (errno != ERANGE) break;
    buffer.resize(buffer.size() * 2);
  }
  return {};
}

std::string homeDir() {
  const char* home = std::getenv("HOME");
  if (home && home[0] != '\0') return trimTrailingSlashes(home);
  struct passwd* entry = getpwuid(getuid());
  if (entry && entry->pw_dir && entry->pw_dir[0] != '\0')
    return trimTrailingSlashes(entry->pw_dir);
  return currentDir();
}

std::string tempDir() {
  const char* tmp = std::getenv("TMPDIR");
  if (tmp && tmp[0] != '\0') return trimTrailingSlashes(tmp);
  return "/tmp";
}

// ================================================================ 文件系统
bool ensureDir(const std::string& path) {
  if (path.empty()) return false;
  return ensureDirRecursive(path, 0755);
}

bool removeTree(const std::string& path) {
  if (path.empty()) return false;
  std::string ignored;
  return removeTreeImpl(path, ignored);
}

bool fileExists(const std::string& path) {
  if (path.empty()) return false;
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && !S_ISDIR(info.st_mode);
}

bool dirExists(const std::string& path) {
  if (path.empty()) return false;
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool writeFile(const std::string& path, const std::string& content) {
  if (path.empty()) return false;
  return writeFileImpl(path, content, nullptr);
}

bool readFile(const std::string& path, std::string& out) {
  if (path.empty()) {
    out.clear();
    return false;
  }
  return readFileImpl(path, out);
}

uint64_t fileSize(const std::string& path) {
  if (path.empty()) return 0;
  struct stat info{};
  if (stat(path.c_str(), &info) != 0) return 0;
  return static_cast<uint64_t>(info.st_size);
}

bool copyTree(const std::string& from, const std::string& to,
              std::string& error) {
  error.clear();
  if (from.empty() || to.empty()) {
    error = "复制失败：源目录或目标目录为空";
    return false;
  }
  return copyTreeImpl(from, to, error);
}

bool makeExecutable(const std::string& path) {
  if (path.empty()) return false;
  struct stat info{};
  if (stat(path.c_str(), &info) != 0) return false;
  const mode_t mode = info.st_mode;
  return chmod(path.c_str(), mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
}

std::string randomHex(size_t bytes) {
  if (bytes == 0) return {};
  if (bytes > 4096) bytes = 4096;
  std::vector<unsigned char> buffer(bytes);
  if (!fillRandomBytes(buffer.data(), buffer.size())) return {};
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  for (unsigned char byte : buffer) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0F]);
  }
  return out;
}

std::string envVar(const std::string& name) {
  if (name.empty()) return {};
  const char* value = std::getenv(name.c_str());
  return value ? std::string(value) : std::string();
}

bool isRunningAsAdmin() { return geteuid() == 0; }

bool killProcessTree(uint64_t pid) {
  if (pid == 0) return false;
  const pid_t target = static_cast<pid_t>(pid);
  bool ok = false;
  if (kill(-target, SIGKILL) == 0) ok = true;
  if (kill(target, SIGKILL) == 0) ok = true;
  if (!ok && errno == ESRCH) return true;  // 已不存在
  return ok;
}

// ================================================================ 进程
std::unique_ptr<Process> spawnProcess(const std::vector<std::string>& args,
                                      const std::string& workDir,
                                      const std::vector<EnvPair>& env,
                                      const std::string& logFile,
                                      std::string& error, bool detached) {
  error.clear();
  if (args.empty() || args[0].empty()) {
    error = "启动失败：命令为空";
    return nullptr;
  }
  std::vector<std::string> environment = mergedEnvironment(env);
  const std::string executable =
      searchExecutable(args[0], environmentValue(environment, "PATH"));
  if (executable.empty()) {
    error = "启动失败：未找到可执行文件 " + args[0];
    return nullptr;
  }
  int logFd = -1;
  if (!logFile.empty()) {
    const size_t slash = logFile.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
      if (!ensureDirRecursive(logFile.substr(0, slash), 0755)) {
        error = "启动失败：无法创建日志目录";
        return nullptr;
      }
    }
    logFd = open(logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                 0644);
    if (logFd < 0) {
      error = "启动失败：无法打开日志文件 " + logFile + "（" + errnoText(errno) +
              "）";
      return nullptr;
    }
  }
  ChildSpawnResult child = spawnChild(args, executable, workDir, environment,
                                      false, detached, logFd);
  if (logFd >= 0) close(logFd);
  if (child.pid < 0) {
    error = "启动失败：" + args[0] + "（" + errnoText(child.error) + "）";
    return nullptr;
  }
  return std::unique_ptr<Process>(new PosixProcess(child.pid));
}

bool runCommand(const std::vector<std::string>& args,
                const std::string& workDir, std::string& output,
                std::string& error, int timeoutMs) {
  output.clear();
  error.clear();
  if (args.empty() || args[0].empty()) {
    error = "命令为空";
    return false;
  }
  CaptureResult result = runCapture(args, workDir, timeoutMs);
  output = result.output;
  if (!result.ok) {
    if (result.error == ENOENT)
      error = "命令不存在：" + args[0];
    else
      error = "命令启动失败：" + args[0] + "（" + errnoText(result.error) + "）";
    return false;
  }
  if (result.timedOut) {
    error = "命令执行超时（超过 " + std::to_string(timeoutMs) + " ms）";
    return false;
  }
  if (result.exitCode != 0) {
    error = "命令执行失败（退出码 " + std::to_string(result.exitCode) + "）";
    return false;
  }
  return true;
}

bool runElevated(const std::vector<std::string>& args, std::string& error) {
  error.clear();
  const std::string self = executablePath();
  if (self.empty()) {
    error = "提权失败：无法定位当前部署器路径";
    return false;
  }
#if defined(__APPLE__)
  // osascript: do shell script "<command>" with administrator privileges
  std::string command = shellQuote(self);
  for (const std::string& arg : args) {
    command += " ";
    command += shellQuote(arg);
  }
  std::string escaped;
  for (char c : command) {
    if (c == '\\')
      escaped += "\\\\";
    else if (c == '"')
      escaped += "\\\"";
    else
      escaped += c;
  }
  const std::vector<std::string> osa = {
      "/usr/bin/osascript", "-e",
      "do shell script \"" + escaped + "\" with administrator privileges"};
  CaptureResult result = runCapture(osa, "", 600000);
  if (!result.ok) {
    error = "提权失败：无法调用 osascript（" + errnoText(result.error) + "）";
    return false;
  }
  if (result.exitCode != 0) {
    if (result.output.find("cancel") != std::string::npos ||
        result.output.find("Cancel") != std::string::npos)
      error = "已取消管理员授权";
    else
      error = "管理员操作失败（osascript 退出码 " +
              std::to_string(result.exitCode) + "）";
    return false;
  }
  return true;
#else
  std::vector<std::string> command;
  if (access("/usr/bin/pkexec", X_OK) == 0) {
    command = {"/usr/bin/pkexec", self};
  } else if (access("/usr/bin/sudo", X_OK) == 0 && geteuid() != 0) {
    // 非交互 sudo：已缓存凭据时可用，否则尽快失败（不阻塞 UI）。
    command = {"/usr/bin/sudo", "-n", "--", self};
  } else {
    error = "提权失败：未找到 pkexec 或 sudo（请安装 polkit 或以 root 运行）";
    return false;
  }
  command.insert(command.end(), args.begin(), args.end());
  CaptureResult result = runCapture(command, "", 600000);
  if (!result.ok) {
    error = "提权失败：无法启动 " + command[0] + "（" + errnoText(result.error) +
            "）";
    return false;
  }
  if (result.exitCode != 0) {
    if (result.exitCode == 126 || result.exitCode == 127) {
      error = "已取消管理员授权（或当前用户不在 sudoers 中）";
    } else {
      error = "管理员操作失败（退出码 " + std::to_string(result.exitCode) + "）";
    }
    return false;
  }
  return true;
#endif
}

bool openBrowser(const std::string& url) {
  if (url.empty()) return false;
#if defined(__APPLE__)
  if (spawnDetachedChecked({"/usr/bin/open", url})) return true;
#else
  if (spawnDetachedChecked({"xdg-open", url})) return true;
  if (spawnDetachedChecked({"gio", "open", url})) return true;
  if (spawnDetachedChecked({"x-www-browser", url})) return true;
#endif
  return false;
}

void revealPath(const std::string& path) {
  if (path.empty()) return;
#if defined(__APPLE__)
  if (fileExists(path)) {
    spawnDetachedChecked({"/usr/bin/open", "-R", path});
    return;
  }
  if (dirExists(path)) {
    spawnDetachedChecked({"/usr/bin/open", path});
    return;
  }
  const std::string parent = parentDirOnly(path);
  if (!parent.empty()) spawnDetachedChecked({"/usr/bin/open", parent});
#else
  if (fileExists(path)) {
    spawnDetachedChecked({"xdg-open", parentDirOnly(path)});
    return;
  }
  if (dirExists(path)) {
    if (spawnDetachedChecked({"xdg-open", path})) return;
    spawnDetachedChecked({"gio", "open", path});
    return;
  }
  const std::string parent = parentDirOnly(path);
  if (!parent.empty()) spawnDetachedChecked({"xdg-open", parent});
#endif
}

}  // namespace mes::platform

#endif  // !_WIN32
