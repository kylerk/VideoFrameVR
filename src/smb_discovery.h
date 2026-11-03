#pragma once

#include <memory>
#include <string>
#include <vector>

// A two-second IPv4 mDNS/WS-Discovery look on a worker thread. No daemon needed.
// Replacement and destruction never wait for the network.
class SmbDiscovery {
 public:
  struct Server {
    std::string name;
    std::string address;
  };
  SmbDiscovery() = default;
  ~SmbDiscovery();
  SmbDiscovery(const SmbDiscovery&) = delete;
  SmbDiscovery& operator=(const SmbDiscovery&) = delete;
  // A new scan replaces an unfinished one; existing results stay until poll().
  void scan();
  // Takes a finished scan; true only if servers() changed.
  bool poll();
  const std::vector<Server>& servers() const { return servers_; }
  // True until poll() takes the finished result.
  bool scanning() const { return bool(pending_); }

 private:
  struct Pending;
  std::shared_ptr<Pending> pending_;
  std::vector<Server> servers_;
};
