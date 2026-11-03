#include "release_update.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kOutputLimit = 16 * 1024;
std::string restartLauncher;  // set by restartAtExit

struct HelperProcess {
  pid_t pid;
  int output;
  bool exited = false;
  ~HelperProcess() {
    close(output);
    // Also on exceptions while collecting output: never leave a child behind.
    if (!exited) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    }
  }
};

// Tags are data, not menu markup or diagnostics. Keep the displayed value short
// and ASCII; the updater is not a source of arbitrary user-facing messages.
bool displayVersion(const std::string& version) {
  if (version.empty() || version.size() > 128) return false;
  for (const unsigned char c : version)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '+'))
      return false;
  return true;
}

}  // namespace

ReleaseUpdate::ReleaseUpdate() {
  const char* helper = std::getenv("VIDEOFRAMEVR_UPDATE_HELPER");
  if (helper && helper[0] == '/') {
    helper_ = helper;
    state_ = State::Idle;
  }
  const char* launcher = std::getenv("VIDEOFRAMEVR_LAUNCHER");
  if (launcher && launcher[0] == '/') launcher_ = launcher;
}

ReleaseUpdate::~ReleaseUpdate() {
  // The worker sees this within 50 ms and kills and reaps the helper, which
  // leaves an interrupted download unused.
  stop_ = true;
  if (worker_.valid()) worker_.wait();
}

void ReleaseUpdate::menuOpened() {
  if (state_ == State::Idle) start(State::Checking);
}

void ReleaseUpdate::download() {
  if (state_ == State::Available || state_ == State::DownloadFailed) start(State::Downloading);
}

void ReleaseUpdate::start(State running) {
  if (worker_.valid()) return;
  const bool update = running == State::Downloading;
  try {
    worker_ = std::async(std::launch::async, [this, update] { return run(update); });
    state_ = running;
  } catch (...) {
    state_ = update ? State::DownloadFailed : State::CheckFailed;
  }
}

void ReleaseUpdate::poll() {
  if (!worker_.valid() || worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  const bool update = state_ == State::Downloading;
  Result result;
  try { result = worker_.get(); } catch (...) {}
  if (!result.success) {
    state_ = update ? State::DownloadFailed : State::CheckFailed;
    return;
  }
  version_ = std::move(result.version);
  state_ = update ? (launcher_.empty() ? State::Ready : State::Restarting)
                  : result.available ? State::Available : State::Current;
}

void ReleaseUpdate::restartAtExit() {
  if (state_ == State::Restarting) restartLauncher = launcher_;
}

void restartIfRequested() {
  if (restartLauncher.empty()) return;
  // The new release's launcher puts its libraries first; leave this
  // release's out of what follows them.
  std::error_code ec;
  const auto lib = std::filesystem::read_symlink("/proc/self/exe", ec).parent_path().parent_path() / "lib";
  if (const char* path = std::getenv("LD_LIBRARY_PATH"); path && !ec) {
    std::string kept;
    std::istringstream entries(path);
    for (std::string entry; std::getline(entries, entry, ':');)
      if (entry != lib.string()) kept += (kept.empty() ? "" : ":") + entry;
    if (kept.empty()) unsetenv("LD_LIBRARY_PATH");
    else setenv("LD_LIBRARY_PATH", kept.c_str(), 1);
  }
  execl(restartLauncher.c_str(), restartLauncher.c_str(), static_cast<char*>(nullptr));
  std::fprintf(stderr, "Could not restart: %s\n", std::strerror(errno));
}

ReleaseUpdate::Result ReleaseUpdate::run(bool update) {
  int output[2];
  if (pipe2(output, O_CLOEXEC) != 0) return {};
  // Launchers can have closed standard descriptors. Keep the pipe above them
  // so opening /dev/null cannot overwrite an endpoint in the spawn actions.
  for (int& fd : output) {
    if (fd > STDERR_FILENO) continue;
    const int copy = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    if (copy < 0) { close(output[0]); close(output[1]); return {}; }
    close(fd);
    fd = copy;
  }
  if (fcntl(output[0], F_SETFL, O_NONBLOCK) < 0) {
    close(output[0]); close(output[1]); return {};
  }
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attributes;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    close(output[0]); close(output[1]); return {};
  }
  if (posix_spawnattr_init(&attributes) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    close(output[0]); close(output[1]); return {};
  }
  const bool configured =
      posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0 &&
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0 &&
      posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO) == 0 &&
      posix_spawn_file_actions_addclose(&actions, output[0]) == 0 &&
      posix_spawn_file_actions_addclose(&actions, output[1]) == 0 &&
      posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0 &&
      posix_spawnattr_setpgroup(&attributes, 0) == 0;
  char* argv[] = {const_cast<char*>(helper_.c_str()),
                  const_cast<char*>(update ? "update" : "check"),
                  const_cast<char*>("--json"), nullptr};
  pid_t child = -1;
  const int spawned = configured
                          ? posix_spawn(&child, helper_.c_str(), &actions, &attributes, argv, environ)
                          : EINVAL;
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  close(output[1]);
  if (spawned != 0) { close(output[0]); return {}; }
  HelperProcess process{child, output[0]};
  const auto deadline = Clock::now() + (update ? std::chrono::seconds(600) : std::chrono::seconds(30));
  std::string body;
  bool eof = false, failed = false, exited = false;
  int status = 0;
  while (!exited || !eof) {
    if (stop_ || Clock::now() >= deadline) { failed = true; break; }
    char buffer[2048];
    const ssize_t n = read(output[0], buffer, sizeof(buffer));
    if (n > 0) {
      if (body.size() + static_cast<size_t>(n) > kOutputLimit) { failed = true; break; }
      body.append(buffer, static_cast<size_t>(n));
    } else if (n == 0) {
      eof = true;
    } else if (errno != EAGAIN && errno != EINTR) {
      failed = true;
      break;
    }
    if (!exited) {
      // Keep the exited child waitable until its output is collected. This
      // reserves its process-group ID for cleanup on failure or exit, even
      // when a descendant is still holding the stdout pipe open.
      siginfo_t info{};
      const int observed = waitid(P_PID, child, &info, WEXITED | WNOHANG | WNOWAIT);
      if (observed == 0 && info.si_pid == child) exited = true;
      else if (observed < 0 && errno != EINTR) { failed = true; break; }
    }
    if ((!exited || !eof) && n <= 0) {
      // Polling a closed pipe returns immediately. An empty poll gives the
      // still-running helper time to exit without spinning the worker.
      pollfd fd{output[0], POLLIN, 0};
      ::poll(eof ? nullptr : &fd, eof ? 0 : 1, 50);
    }
  }
  if (failed || !exited) {
    kill(-child, SIGTERM);
    const auto grace = Clock::now() + std::chrono::milliseconds(250);
    while (!exited && Clock::now() < grace) {
      const pid_t reaped = waitpid(child, &status, WNOHANG);
      if (reaped == child || (reaped < 0 && errno == ECHILD))
        process.exited = exited = true;
      else ::poll(nullptr, 0, 10);
    }
    kill(-child, SIGKILL);
  }
  if (!process.exited) {
    pid_t reaped;
    do { reaped = waitpid(child, &status, 0); } while (reaped < 0 && errno == EINTR);
    if (reaped != child) return {};
    process.exited = true;
  }
  if (failed || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
  const auto json = nlohmann::json::parse(body, nullptr, false);
  if (!json.is_object() || !json.contains("status") || !json["status"].is_string() ||
      !json.contains("version") || !json["version"].is_string() ||
      !json.contains("message") || !json["message"].is_string()) return {};
  const std::string result = json["status"].get<std::string>();
  const std::string version = json["version"].get<std::string>();
  if (!displayVersion(version) || (update ? result != "ready"
                                         : result != "available" && result != "current")) return {};
  return {true, result == "available", version};
}
