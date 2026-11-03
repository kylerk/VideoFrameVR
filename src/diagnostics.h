#pragma once

#include <mutex>
#include <string>
#include <thread>

// "Save diagnostics" (docs/alpha/diagnostics.md): the run logs and the
// settings, anonymised, in one zip in the Downloads folder, for a bug report.

// Writes the zip; returns its path, or "" with `error` set.
std::string saveDiagnostics(std::string& error);

// The menu's button: saves off the render thread.
class DiagnosticsExport {
 public:
  enum class State { Idle, Saving, Saved, Failed };

  ~DiagnosticsExport();
  void start();
  void poll();  // takes a finished save's result
  State state() const { return state_; }
  const std::string& fileName() const { return fileName_; }  // Saved: the zip's name

 private:
  std::thread worker_;
  std::mutex lock_;
  bool done_ = false;
  std::string result_;  // the zip's path, "" if it failed
  State state_ = State::Idle;
  std::string fileName_;
};
