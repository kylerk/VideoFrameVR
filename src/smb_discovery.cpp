#include "smb_discovery.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <map>
#include <random>
#include <sstream>
#include <string_view>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string lower(std::string text) {
  for (char& c : text) c = std::tolower(static_cast<unsigned char>(c));
  return text;
}

std::string uuid() {
  std::random_device random;
  const char* hex = "0123456789abcdef";
  std::string id = "urn:uuid:";
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) id += '-';
    unsigned byte = random() & 255;
    if (i == 6) byte = (byte & 15) | 64;
    if (i == 8) byte = (byte & 63) | 128;
    id += hex[byte >> 4];
    id += hex[byte & 15];
  }
  return id;
}

std::string xmlEscape(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

// Match local element names, not the responder's choice of namespace prefix.
std::string element(std::string_view xml, std::string_view name) {
  size_t pos = 0;
  while ((pos = xml.find('<', pos)) != xml.npos) {
    const size_t start = ++pos;
    const size_t end = xml.find_first_of(" \t\r\n>/", start);
    if (end == xml.npos) break;
    auto tag = xml.substr(start, end - start);
    auto local = tag.substr(tag.find(':') == tag.npos ? 0 : tag.find(':') + 1);
    if (local != name) continue;
    const size_t body = xml.find('>', end);
    if (body == xml.npos) break;
    const size_t close = xml.find("</" + std::string(tag) + ">", body + 1);
    if (close == xml.npos) break;
    return std::string(xml.substr(body + 1, close - body - 1));
  }
  return {};
}

std::string xmlText(std::string text) {
  const std::pair<const char*, const char*> entities[] = {
      {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
  for (const auto& e : entities) {
    size_t pos = 0;
    while ((pos = text.find(e.first, pos)) != text.npos) {
      text.replace(pos, std::char_traits<char>::length(e.first), e.second);
      ++pos;
    }
  }
  return text;
}

std::string soap(const std::string& to, const std::string& action, const std::string& body) {
  return "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
      "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
      "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
      "xmlns:wsdp=\"http://schemas.xmlsoap.org/ws/2006/02/devprof\" "
      "xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\">"
      "<s:Header><a:To>" + xmlEscape(to) + "</a:To><a:Action>" + action +
      "</a:Action><a:MessageID>" + uuid() + "</a:MessageID><a:ReplyTo><a:Address>"
      "http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous"
      "</a:Address></a:ReplyTo></s:Header><s:Body>" + body + "</s:Body></s:Envelope>";
}

int udpSocket() {
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd >= 0) {
    const int ttl = 255;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  }
  return fd;
}

void sendQuery(int fd, const char* group, int port, const std::string& query) {
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(port);
  inet_pton(AF_INET, group, &dest.sin_addr);
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) return;
  for (auto* it = interfaces; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
        !(it->ifa_flags & IFF_UP) || !(it->ifa_flags & IFF_MULTICAST) ||
        (it->ifa_flags & IFF_LOOPBACK)) continue;
    const auto addr = reinterpret_cast<sockaddr_in*>(it->ifa_addr)->sin_addr;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr)) == 0)
      sendto(fd, query.data(), query.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
  }
  freeifaddrs(interfaces);
}

unsigned word(const std::string& packet, size_t pos) {
  return (static_cast<unsigned char>(packet[pos]) << 8) |
      static_cast<unsigned char>(packet[pos + 1]);
}

bool dnsName(const std::string& packet, size_t& pos, std::string& name) {
  size_t at = pos;
  bool jumped = false;
  name.clear();
  for (size_t steps = 0; steps < packet.size(); ++steps) {
    if (at >= packet.size()) return false;
    const unsigned len = static_cast<unsigned char>(packet[at++]);
    if ((len & 192) == 192) {
      if (at >= packet.size()) return false;
      const size_t target = ((len & 63) << 8) | static_cast<unsigned char>(packet[at++]);
      if (!jumped) pos = at;
      jumped = true;
      at = target;
    } else {
      if (len > 63 || at + len > packet.size()) return false;
      if (!jumped) pos = at + len;
      if (!len) return true;
      if (!name.empty()) name += '.';
      name.append(packet, at, len);
      if (name.size() > 255) return false;
      at += len;
    }
  }
  return false;
}

struct Mdns {
  std::map<std::string, std::string> names, targets, addresses, sources;

  void receive(const std::string& packet, const std::string& source) {
    if (packet.size() < 12 || !(word(packet, 2) & 0x8000)) return;
    size_t pos = 12;
    std::string owner, value;
    for (unsigned i = 0; i < word(packet, 4); ++i) {
      if (!dnsName(packet, pos, owner) || pos + 4 > packet.size()) return;
      pos += 4;
    }
    const unsigned count = word(packet, 6) + word(packet, 8) + word(packet, 10);
    for (unsigned i = 0; i < count; ++i) {
      if (!dnsName(packet, pos, owner) || pos + 10 > packet.size()) return;
      const unsigned type = word(packet, pos), cls = word(packet, pos + 2) & 0x7fff;
      const size_t end = pos + 10 + word(packet, pos + 8);
      pos += 10;
      if (end > packet.size()) return;
      owner = lower(owner);
      if (cls == 1 && type == 12 && owner == "_smb._tcp.local") {
        if (dnsName(packet, pos, value) && pos <= end) {
          const std::string key = lower(value), suffix = "._smb._tcp.local";
          if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            names[key] = value.substr(0, value.size() - suffix.size());
            sources[key] = source;
          }
        }
      } else if (cls == 1 && type == 33 && pos + 6 <= end) {
        pos += 6;
        if (dnsName(packet, pos, value) && pos <= end) targets[owner] = lower(value);
      } else if (cls == 1 && type == 1 && end - pos == 4) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, packet.data() + pos, ip, sizeof(ip));
        addresses[owner] = ip;
      }
      pos = end;
    }
  }
};

struct Metadata {
  CURL* curl = nullptr;
  std::string request, response;
};

size_t writeMetadata(char* data, size_t size, size_t count, void* opaque) {
  auto& response = static_cast<Metadata*>(opaque)->response;
  const size_t bytes = size * count;
  if (response.size() + bytes > 256 * 1024) return 0;
  response.append(data, bytes);
  return bytes;
}

std::vector<SmbDiscovery::Server> discover(const std::atomic<bool>& cancelled) {
  const auto start = Clock::now(), deadline = start + 2s;
  const int mdnsFd = udpSocket(), wsdFd = udpSocket();
  const std::string mdnsQuery("\x12\x34\0\0\0\1\0\0\0\0\0\0\4_smb\4_tcp\5local\0\0\x0c\0\1", 33);
  // Stock wsdd accepts only Device, not the Device + Computer type list.
  const std::string probe = soap("urn:schemas-xmlsoap-org:ws:2005:04:discovery",
      "http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe",
      "<d:Probe><d:Types>wsdp:Device</d:Types></d:Probe>");
  struct Query { std::string text; Clock::time_point retry; };
  std::vector<Query> queries;
  auto sendWsd = [&](const std::string& text) {
    sendQuery(wsdFd, "239.255.255.250", 3702, text);
    queries.push_back({text, Clock::now() + 300ms});
  };
  sendQuery(mdnsFd, "224.0.0.251", 5353, mdnsQuery);
  sendWsd(probe);
  bool mdnsRetried = false;
  Mdns mdns;
  std::map<std::string, std::string> servers;
  std::map<std::string, bool> resolved;
  std::map<std::string, Metadata> metadata;
  CURLM* multi = curl_multi_init();
  curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/soap+xml");

  while (!cancelled.load() && Clock::now() < deadline) {
    const auto now = Clock::now();
    if (!mdnsRetried && now >= start + 300ms) {
      sendQuery(mdnsFd, "224.0.0.251", 5353, mdnsQuery);
      mdnsRetried = true;
    }
    for (auto& query : queries) {
      if (now >= query.retry) {
        sendQuery(wsdFd, "239.255.255.250", 3702, query.text);
        query.retry = Clock::time_point::max();
      }
    }
    for (int fd : {mdnsFd, wsdFd}) {
      char buffer[65536];
      sockaddr_in source{};
      socklen_t length = sizeof(source);
      const auto bytes = recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&source), &length);
      if (bytes <= 0) continue;
      char ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &source.sin_addr, ip, sizeof(ip));
      const std::string packet(buffer, bytes);
      if (fd == mdnsFd) {
        mdns.receive(packet, ip);
        continue;
      }
      std::string body = element(packet, "ProbeMatch");
      const bool match = !body.empty();
      if (!match) body = element(packet, "ResolveMatch");
      if (body.empty()) continue;
      const std::string types = element(body, "Types");
      if (match && types.find(":Computer") == types.npos && types != "Computer") continue;
      servers.emplace(ip, ip);
      const std::string endpoint = xmlText(element(body, "Address"));
      std::istringstream xaddrs(xmlText(element(body, "XAddrs")));
      std::string url;
      while (xaddrs >> url) {
        // Do not let an unauthenticated multicast reply direct HTTP off-host.
        const std::string host = "http://" + std::string(ip);
        if (url.find('@') == url.npos && url.compare(0, host.size(), host) == 0 && url.size() > host.size() &&
            (url[host.size()] == ':' || url[host.size()] == '/')) break;
        url.clear();
      }
      if (url.empty()) {
        if (match && !endpoint.empty() && !resolved[ip]) {
          resolved[ip] = true;
          sendWsd(soap("urn:schemas-xmlsoap-org:ws:2005:04:discovery",
              "http://schemas.xmlsoap.org/ws/2005/04/discovery/Resolve",
              "<d:Resolve><a:EndpointReference><a:Address>" + xmlEscape(endpoint) +
              "</a:Address></a:EndpointReference></d:Resolve>"));
        }
        continue;
      }
      if (!multi || metadata.count(ip)) continue;
      auto& get = metadata[ip];
      get.curl = curl_easy_init();
      if (!get.curl) continue;
      get.request = soap(endpoint, "http://schemas.xmlsoap.org/ws/2004/09/transfer/Get", "");
      curl_easy_setopt(get.curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(get.curl, CURLOPT_PROXY, "");
      curl_easy_setopt(get.curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(get.curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
      curl_easy_setopt(get.curl, CURLOPT_TIMEOUT_MS,
          std::max(1L, std::min(700L, long(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()))));
      curl_easy_setopt(get.curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(get.curl, CURLOPT_POSTFIELDS, get.request.c_str());
      curl_easy_setopt(get.curl, CURLOPT_WRITEFUNCTION, writeMetadata);
      curl_easy_setopt(get.curl, CURLOPT_WRITEDATA, &get);
      curl_multi_add_handle(multi, get.curl);
    }
    if (multi) {
      int running = 0, left = 0;
      curl_multi_perform(multi, &running);
      while (CURLMsg* msg = curl_multi_info_read(multi, &left)) {
        if (msg->msg != CURLMSG_DONE) continue;
        for (auto& item : metadata) {
          if (item.second.curl != msg->easy_handle) continue;
          if (msg->data.result == CURLE_OK) {
            std::string name = xmlText(element(item.second.response, "Computer"));
            name = name.substr(0, name.find('/'));
            if (!name.empty()) servers[item.first] = std::move(name);
          }
          curl_multi_remove_handle(multi, item.second.curl);
          curl_easy_cleanup(item.second.curl);
          item.second.curl = nullptr;
          break;
        }
      }
    }
    pollfd sockets[] = {{mdnsFd, POLLIN, 0}, {wsdFd, POLLIN, 0}};
    ::poll(sockets, 2, 10);
  }
  for (auto& item : metadata) {
    if (!item.second.curl) continue;
    curl_multi_remove_handle(multi, item.second.curl);
    curl_easy_cleanup(item.second.curl);
  }
  if (multi) curl_multi_cleanup(multi);
  curl_slist_free_all(headers);
  if (mdnsFd >= 0) close(mdnsFd);
  if (wsdFd >= 0) close(wsdFd);
  for (const auto& item : mdns.names) {
    const auto addr = mdns.addresses.find(mdns.targets[item.first]);
    const std::string ip = addr == mdns.addresses.end() ? mdns.sources[item.first] : addr->second;
    servers[ip] = item.second;
  }
  std::vector<SmbDiscovery::Server> result;
  for (auto& item : servers) result.push_back({std::move(item.second), item.first});
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    const auto an = lower(a.name), bn = lower(b.name);
    return an == bn ? a.address < b.address : an < bn;
  });
  return result;
}

}  // namespace

struct SmbDiscovery::Pending {
  std::atomic<bool> cancelled{false}, done{false};
  std::vector<Server> servers;
};

SmbDiscovery::~SmbDiscovery() {
  if (pending_) pending_->cancelled = true;
}

void SmbDiscovery::scan() {
  if (pending_) pending_->cancelled = true;
  auto job = std::make_shared<Pending>();
  pending_ = job;
  std::thread([job] {
    job->servers = discover(job->cancelled);
    job->done = true;
  }).detach();
}

bool SmbDiscovery::poll() {
  if (!pending_ || !pending_->done.load()) return false;
  const auto& result = pending_->servers;
  const bool same = servers_.size() == result.size() &&
      std::equal(servers_.begin(), servers_.end(), result.begin(), [](const auto& a, const auto& b) {
        return a.name == b.name && a.address == b.address;
      });
  if (!same) servers_ = std::move(pending_->servers);
  pending_.reset();
  return !same;
}
