#pragma once

#include <atomic>
#include <future>
#include <string>

// Updates for the packaged GitHub install, which opts in through its stable
// launcher. The helper owns release identity, network access and installation
// (docs/release-update-protocol.md). All calls are on the main thread; the
// helper runs on a worker.
class ReleaseUpdate {
 public:
  // Ready: installed, but the launcher didn't say where it is (an older
  // install's), so the app can't restart itself. Restarting: it can.
  enum class State { Disabled, Idle, Checking, Current, Available, Downloading, Ready,
                     Restarting, CheckFailed, DownloadFailed };
  ReleaseUpdate();
  ~ReleaseUpdate();
  ReleaseUpdate(const ReleaseUpdate&) = delete;
  ReleaseUpdate& operator=(const ReleaseUpdate&) = delete;

  void menuOpened();  // the first open menu checks quietly, once per run
  void download();    // installs the available release for the next launch
  void poll();        // nonblocking
  void restartAtExit();  // Restarting: the launcher starts the new release once the app quits
  State state() const { return state_; }
  const std::string& version() const { return version_; }

 private:
  struct Result {
    bool success = false, available = false;
    std::string version;
  };
  void start(State running);
  Result run(bool update);
  std::string helper_, launcher_, version_;
  State state_ = State::Disabled;
  std::atomic<bool> stop_{false};
  std::future<Result> worker_;
};

// Registered with atexit before anything else in main, so it runs after a
// quit has saved and shut down everything, the run log included. After
// restartAtExit, it execs the launcher in this process, which Steam keeps
// tracking by its ID.
void restartIfRequested();
