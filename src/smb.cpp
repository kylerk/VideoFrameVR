#include "smb.h"

#include "file_util.h"
#include "read_ahead.h"
#include "run_log.h"
#include "settings.h"

#include <mpv/stream_cb.h>
#include <nlohmann/json.hpp>
#include <smb2/smb2.h>
#include <smb2/smb2-errors.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace smb {
bool isSmb(std::string_view location) { return location.substr(0, 6) == "smb://"; }

std::string host(const std::string& locator) {
  if (!isSmb(locator)) return {};
  return lowerCase(locator.substr(6, locator.find('/', 6) - 6));
}

std::string parent(const std::string& locator) {
  if (!isSmb(locator)) return {};
  const size_t end = locator.find_last_not_of('/');
  const size_t slash = locator.rfind('/', end);
  return slash < 6 ? std::string() : locator.substr(0, slash);
}

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t kChunk = ReadAhead::kBlock;
std::mutex contextList;  // libsmb2's process-wide context list

struct Location {
  std::string server, share, path;
  explicit Location(const std::string& locator) : server(host(locator)) {
    const size_t slash = locator.find('/', 6);
    if (slash == std::string::npos) return;
    const size_t end = locator.find('/', slash + 1);
    share = locator.substr(slash + 1, end - slash - 1);
    if (end != std::string::npos) path = locator.substr(end + 1);
    hideInRunLog(server, "<smb-host>");
    if (!share.empty()) hideInRunLog(share, "<smb-share>");
  }
};

Status statusOf(int rc) {
  if (rc >= 0) return Status::Ok;
  if (rc == -ENOENT || rc == -ENOTDIR) return Status::Missing;
  if (rc == -EACCES || rc == -EPERM) return Status::Denied;
  return Status::Offline;
}

bool connectionLost(int rc, uint32_t nt = 0) {
  if (rc == -ENETRESET)
    return nt != SMB2_STATUS_FILE_CLOSED && nt != SMB2_STATUS_CANCELLED;
  return rc == -ECONNRESET || rc == -ECONNABORTED || rc == -ENOTCONN ||
         rc == -EPIPE || rc == -ETIMEDOUT ||
         (rc < 0 && nt == SMB2_STATUS_USER_SESSION_DELETED);
}

struct Result {
  bool done = false;
  int status = 0;
  void* data = nullptr;
  uint32_t tree = 0;
  uint32_t nt = 0;
  static void cb(smb2_context* ctx, int status, void* data, void* opaque) {
    auto& r = *static_cast<Result*>(opaque);
    r.done = true;
    r.status = status;
    r.data = data;
    r.nt = status < 0 ? smb2_get_nterror(ctx) : 0;
    smb2_get_tree_id_for_pdu(ctx, nullptr, &r.tree);
  }
  static void treeCb(smb2_context* ctx, int status, void*, void* opaque) {
    cb(ctx, status ? -nterror_to_errno(status) : 0, nullptr, opaque);
    static_cast<Result*>(opaque)->nt = status;
  }
};

struct Handle {
  smb2fh* fh = nullptr;
  uint32_t tree = 0;
  uint64_t epoch = 0, length = 0;
};

struct Stream : ReadAhead {
  Handle handle;
  std::string locator;
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<bool> cancelled{false};
  bool failed = false, disconnected = false, closing = false, closed = false;
  bool reconnecting = false;
  bool background = false;  // a download's: reads only while no playback stream is open
  Clock::time_point retryUntil{};

  static void readDone(smb2_context* ctx, int status, void*, void* opaque) {
    auto& b = *static_cast<Block*>(opaque);
    auto& s = static_cast<Stream&>(*b.owner);
    std::lock_guard lock(s.mutex);
    b.pending = false;
    if (s.reconnecting || s.closed) return;
    if (b.generation != s.generation) b.done = true;
    else if (status <= 0) {
      s.disconnected = connectionLost(status, smb2_get_nterror(ctx));
      s.reconnecting = s.disconnected;
      s.failed = !s.disconnected;
    } else {
      s.retryUntil = {};
      b.filled += status;
      b.done = b.filled == b.bytes.size();
    }
    s.cv.notify_all();
  }
};

// All libsmb2 calls for a server run here. A job may wait for its one request;
// pump() keeps playback's two-block windows filled during that wait.
struct Connection {
  std::string server;
  smb2_context* ctx = nullptr;
  uint64_t epoch = 0;
  uint32_t serviceTree = 0;
  std::map<std::string, uint32_t> trees;
  std::vector<std::shared_ptr<Stream>> streams;
  int wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  std::mutex mutex;
  std::deque<std::function<void()>> urgent, background;
  bool stop = false;
  Clock::time_point retryAt{}, deadline{};
  std::thread worker;

  explicit Connection(std::string name) : server(std::move(name)), worker([this] { run(); }) {}
  ~Connection() {
    { std::lock_guard lock(mutex); stop = true; }
    notify();
    worker.join();
    ::close(wake);
  }
  void notify() { uint64_t n = 1; (void)::write(wake, &n, sizeof(n)); }
  void post(std::function<void()> job, bool priority = false) {
    { std::lock_guard lock(mutex); (priority ? urgent : background).push_back(std::move(job)); }
    notify();
  }
  template<class F> auto call(F fn, bool priority = false) {
    auto task = std::make_shared<std::packaged_task<decltype(fn())()>>(std::move(fn));
    auto result = task->get_future();
    post([task] { (*task)(); }, priority);
    return result.get();
  }
  void destroy(bool retry = false) {
    for (auto& s : streams) {
      std::lock_guard lock(s->mutex);
      s->reconnecting = retry && !s->failed && !s->cancelled && !s->closing;
      if (!s->reconnecting) s->failed = s->closed = true;
      if (s->reconnecting && s->retryUntil == Clock::time_point{})
        s->retryUntil = Clock::now() + std::chrono::seconds(10);
    }
    if (ctx) {
      std::lock_guard lock(contextList);
      smb2_destroy_context(ctx);  // completes outstanding callbacks before freeing their data
    }
    ctx = nullptr;
    trees.clear();
    serviceTree = 0;
    ++epoch;
    for (auto& s : streams) {
      std::lock_guard lock(s->mutex);
      s->blocks.clear();
      s->disconnected = false;
      s->failed = s->closed = !s->reconnecting;
      s->cv.notify_all();
    }
    if (!retry) streams.clear();
  }
  void reconnect() {
    const auto it = std::find_if(streams.begin(), streams.end(),
                                 [](auto& s) { return s->reconnecting && !s->closed; });
    if (it == streams.end()) return;
    const auto s = *it;
    if (Clock::now() < retryAt) return;
    Status status = Status::Offline;
    Handle handle;
    if (!s->cancelled && Clock::now() < s->retryUntil) {
      deadline = s->retryUntil;
      status = open(Location(s->locator), handle);
      deadline = {};
    }
    std::lock_guard lock(s->mutex);
    if (status == Status::Ok) {
      s->handle = handle;
      ++s->generation;
      s->windows.clear();
      s->reconnecting = false;
    } else if (s->cancelled || status != Status::Offline || Clock::now() >= s->retryUntil) {
      s->failed = s->closed = true;
      s->reconnecting = false;
      s->cv.notify_all();
    } else {
      retryAt = Clock::now() + std::chrono::milliseconds(250);
    }
  }
  void fill() {
    streams.erase(std::remove_if(streams.begin(), streams.end(),
                                 [](auto& s) { return s->closed; }), streams.end());
    if (!ctx) return;
    const bool playing = std::any_of(streams.begin(), streams.end(),
                                     [](auto& s) { return !s->background && !s->closed; });
    for (auto& s : streams) {
      std::lock_guard lock(s->mutex);
      if (s->closed || s->closing || s->reconnecting) continue;
      smb2_select_tree_id(ctx, s->handle.tree);
      if (s->cancelled || s->failed) {
        if (std::any_of(s->blocks.begin(), s->blocks.end(), [](auto& b) { return b.pending; }))
          continue;
        s->closing = true;
        const int rc = smb2_close_async(ctx, s->handle.fh,
            [](smb2_context*, int, void*, void* p) { static_cast<Stream*>(p)->closed = true; },
            s.get());
        if (rc < 0) s->closed = true;
        continue;
      }
      if (s->windows.empty()) s->seek(s->position);
      s->retire();
      if (s->background && playing) continue;
      s->queue(std::min(kChunk, smb2_get_max_read_size(ctx)), s->handle.length);
      for (auto& b : s->blocks) {
        if (b.pending || b.done || b.generation != s->generation) continue;
        b.pending = true;
        const int rc = smb2_pread_async(ctx, s->handle.fh, b.bytes.data() + b.filled,
            b.bytes.size() - b.filled, b.offset + b.filled, Stream::readDone, &b);
        if (rc < 0) {
          b.pending = false;
          s->disconnected = connectionLost(rc);
          s->reconnecting = s->disconnected;
          s->failed = !s->disconnected;
          s->cv.notify_all();
        }
      }
    }
  }
  bool pump(int timeout = 20) {
    fill();
    pollfd fds[2] = {{ctx ? smb2_get_fd(ctx) : -1,
                     ctx ? static_cast<short>(smb2_which_events(ctx)) : short(0), 0},
                    {wake, POLLIN, 0}};
    const int rc = poll(fds, 2, timeout);
    if (fds[1].revents) { uint64_t n; (void)::read(wake, &n, sizeof(n)); }
    // Directory/RPC callbacks can enqueue another request on their tree.
    if (ctx && serviceTree) smb2_select_tree_id(ctx, serviceTree);
    if ((rc < 0 && errno != EINTR) || (ctx && smb2_service(ctx, fds[0].revents) < 0)) return false;
    return std::none_of(streams.begin(), streams.end(),
                        [](auto& s) { return s->disconnected && !s->closed; });
  }
  int wait(Result& r, int rc) {
    if (rc < 0) {
      if (connectionLost(rc)) destroy(true);
      return rc;
    }
    auto until = Clock::now() + std::chrono::seconds(5);
    if (deadline != Clock::time_point{}) until = std::min(until, deadline);
    while (rc >= 0 && !r.done && Clock::now() < until) {
      if (!pump()) { rc = -EIO; break; }
    }
    if (rc < 0 || !r.done) {
      destroy(true);
      return rc < 0 ? rc : -ETIMEDOUT;
    }
    if (connectionLost(r.status, r.nt)) destroy(true);
    return r.status;
  }
  Status connect(const std::string& share) {
    Login login;
    bool remembered;
    if (!knownLogin(server, login, remembered)) return Status::NeedsLogin;
    if (!ctx) {
      { std::lock_guard lock(contextList); ctx = smb2_init_context(); }
      if (!ctx) return Status::Offline;
      std::string user = login.user;
      const auto slash = user.find('\\');
      if (slash != std::string::npos) {
        smb2_set_domain(ctx, user.substr(0, slash).c_str());
        user.erase(0, slash + 1);
      }
      smb2_set_user(ctx, user.c_str());
      smb2_set_password(ctx, login.password.c_str());
      smb2_set_security_mode(ctx, SMB2_NEGOTIATE_SIGNING_ENABLED);
      smb2_set_timeout(ctx, 5);
      // Authenticate once; a refused share below is Denied, not NeedsLogin.
      Result r;
      const int rc = wait(r, smb2_connect_share_async(ctx, server.c_str(), "IPC$",
                                                    user.c_str(), Result::cb, &r));
      if (rc < 0) {
        destroy(true);
        switch (r.nt) {
          case SMB2_STATUS_LOGON_FAILURE:
          case SMB2_STATUS_ACCESS_DENIED:
          case SMB2_STATUS_WRONG_PASSWORD:
          case SMB2_STATUS_ACCOUNT_DISABLED:
          case SMB2_STATUS_ACCOUNT_RESTRICTION:
          case SMB2_STATUS_ACCOUNT_LOCKED_OUT:
          case SMB2_STATUS_PASSWORD_EXPIRED:
          case SMB2_STATUS_PASSWORD_MUST_CHANGE: return Status::NeedsLogin;
          default: return statusOf(rc);
        }
      }
      trees["IPC$"] = r.tree;
    }
    if (!trees.count(share)) {
      const std::string unc = "\\\\" + server + "\\" + share;
      auto* utf16 = smb2_utf8_to_utf16(unc.c_str());
      if (!utf16) return Status::Missing;
      smb2_tree_connect_request req{};
      req.path_length = utf16->len * 2;
      req.path = utf16->val;
      Result r;
      auto* pdu = smb2_cmd_tree_connect_async(ctx, &req, Result::treeCb, &r);
      free(utf16);
      if (!pdu) return Status::Offline;
      smb2_queue_pdu(ctx, pdu);
      const int rc = wait(r, 0);
      if (rc < 0) return statusOf(rc);
      trees[share] = r.tree;
    }
    serviceTree = trees[share];
    smb2_select_tree_id(ctx, serviceTree);
    return Status::Ok;
  }
  Status open(const Location& loc, Handle& h) {
    Status status = connect(loc.share);
    if (status != Status::Ok) return status;
    Result opened;
    int rc = wait(opened, smb2_open_async(ctx, loc.path.c_str(), O_RDONLY, Result::cb, &opened));
    if (rc < 0) return statusOf(rc);
    h = {static_cast<smb2fh*>(opened.data), serviceTree, epoch, 0};
    smb2_stat_64 st{};
    Result r;
    rc = wait(r, smb2_fstat_async(ctx, h.fh, &st, Result::cb, &r));
    if (rc < 0) { close(h); h.fh = nullptr; return statusOf(rc); }
    h.length = st.smb2_size;
    return Status::Ok;
  }
  // One request of the raw API on the share connect() chose; its NT status
  // in r.nt.
  int send(Result& r, smb2_pdu* pdu) {
    if (!pdu) return -ENOMEM;
    smb2_queue_pdu(ctx, pdu);
    return wait(r, 0);
  }
  Status unlink(const Location& loc, uint64_t size, int64_t modified) {
    Status status = connect(loc.share);
    if (status != Status::Ok) return status;
    // The file itself, never a folder, a link not followed; opened to be
    // deleted, letting others go on reading it meanwhile but not writing
    // (one open to write: a sharing violation, InUse).
    smb2_create_request req{};
    req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
    req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
    req.desired_access = SMB2_DELETE | SMB2_FILE_READ_ATTRIBUTES;
    req.share_access = SMB2_FILE_SHARE_READ | SMB2_FILE_SHARE_DELETE;
    req.create_disposition = SMB2_FILE_OPEN;
    req.create_options = SMB2_FILE_NON_DIRECTORY_FILE | SMB2_FILE_OPEN_REPARSE_POINT;
    req.name = loc.path.c_str();
    struct Opened {
      Result result;
      smb2_create_reply reply{};
      static void cb(smb2_context* ctx, int status, void* data, void* opaque) {
        auto& o = *static_cast<Opened*>(opaque);
        if (status == 0) o.reply = *static_cast<smb2_create_reply*>(data);
        Result::treeCb(ctx, status, nullptr, &o.result);
      }
    } opened;
    const int rc = send(opened.result, smb2_cmd_create_async(ctx, &req, Opened::cb, &opened));
    if (rc < 0) {
      switch (opened.result.nt) {
        case SMB2_STATUS_SHARING_VIOLATION: return Status::InUse;
        case SMB2_STATUS_DELETE_PENDING: return Status::Missing;
        case SMB2_STATUS_ACCESS_DENIED:
        case SMB2_STATUS_CANNOT_DELETE:
        case SMB2_STATUS_MEDIA_WRITE_PROTECTED: return Status::Denied;
        case SMB2_STATUS_FILE_IS_A_DIRECTORY: return Status::Changed;
        default: return statusOf(rc);
      }
    }
    // Windows' times count 100 ns steps from 1601.
    const smb2_create_reply& file = opened.reply;
    const bool same = file.end_of_file == size &&
                      static_cast<int64_t>(file.last_write_time / 10000000) - 11644473600 == modified &&
                      !(file.file_attributes & (SMB2_FILE_ATTRIBUTE_DIRECTORY |
                                                SMB2_FILE_ATTRIBUTE_REPARSE_POINT));
    status = Status::Changed;
    if (same && ctx) {
      smb2_file_disposition_info gone{1};
      smb2_set_info_request mark{};
      mark.info_type = SMB2_0_INFO_FILE;
      mark.file_info_class = SMB2_FILE_DISPOSITION_INFORMATION;
      std::memcpy(mark.file_id, file.file_id, SMB2_FD_SIZE);
      mark.input_data = &gone;
      Result r;
      const int rc = send(r, smb2_cmd_set_info_async(ctx, &mark, Result::treeCb, &r));
      status = rc < 0 && r.nt == SMB2_STATUS_CANNOT_DELETE ? Status::Denied : statusOf(rc);
    }
    // Deleted as it closes (or as the server drops a broken connection).
    if (ctx) {
      smb2_close_request close{};
      std::memcpy(close.file_id, file.file_id, SMB2_FD_SIZE);
      Result r;
      send(r, smb2_cmd_close_async(ctx, &close, Result::treeCb, &r));
    }
    return status;
  }
  void close(Handle h) {
    if (!ctx || h.epoch != epoch || !h.fh) return;
    serviceTree = h.tree;
    smb2_select_tree_id(ctx, h.tree);
    Result r;
    wait(r, smb2_close_async(ctx, h.fh, Result::cb, &r));
  }
  void run() {
    for (;;) {
      std::function<void()> job;
      {
        std::lock_guard lock(mutex);
        if (stop && urgent.empty() && background.empty()) break;
        auto& jobs = urgent.empty() ? background : urgent;
        if (!jobs.empty()) { job = std::move(jobs.front()); jobs.pop_front(); }
      }
      reconnect();
      fill();
      if (job) job();
      serviceTree = 0;
      // wait() may have consumed another queued job's wake-up.
      if (!pump(job ? 0 : 20)) destroy(true);
    }
    destroy();
  }
};

struct SavedLogin {
  Login login;
  std::string name;
  bool remembered = false;
};
struct Registry {
  std::mutex mutex;
  std::map<std::string, SavedLogin> logins;
  std::map<std::string, std::shared_ptr<Connection>> connections;
  Registry() {
    std::ifstream in(configDir() / "smb-logins.json");
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (!it.value().is_object()) continue;
      const auto& v = it.value();
      if (!v.contains("user") || !v["user"].is_string() ||
          !v.contains("password") || !v["password"].is_string()) continue;
      logins[lowerCase(it.key())] = {{v["user"], v["password"]}, v.value("name", it.key()), true};
      hideInRunLog(it.key(), "<smb-host>");
    }
  }
  void save() {
    nlohmann::json j = nlohmann::json::object();
    for (auto& [host, saved] : logins) if (saved.remembered)
      j[host] = {{"user", saved.login.user}, {"password", saved.login.password},
                 {"name", saved.name}};
    queueConfigWork([contents = j.dump()] {
      const auto dir = configDir();
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      if (ec || chmod(dir.c_str(), 0700) != 0) return;
      const auto path = dir / "smb-logins.json";
      const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
      if (fd < 0) return;
      if (fchmod(fd, 0600) == 0) {
        size_t offset = 0;
        while (offset < contents.size()) {
          const auto n = ::write(fd, contents.data() + offset, contents.size() - offset);
          if (n <= 0) break;
          offset += n;
        }
      }
      ::close(fd);
    });
  }
};
// Never destroyed: detached threads (first-run setup's counts and share
// lists, a sign-in) and the connections' workers may still use it while the
// program exits, and a connection it ended then would wait for its worker's
// request (up to 5 s each, plus looking up the server's name, which has no
// limit).
Registry& registry() {
  static Registry& r = *new Registry;
  return r;
}

std::shared_ptr<Connection> connection(const std::string& host) {
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  auto& c = r.connections[host];
  if (!c) c = std::make_shared<Connection>(host);
  return c;
}

Entry entry(const std::string& name, const smb2_stat_64& st) {
  return {name, st.smb2_type == SMB2_TYPE_DIRECTORY, st.smb2_size,
          static_cast<int64_t>(st.smb2_mtime)};
}
}  // namespace

bool knownLogin(const std::string& host, Login& login, bool& remembered) {
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  const auto it = r.logins.find(lowerCase(host));
  if (it == r.logins.end()) return false;
  login = it->second.login;
  remembered = it->second.remembered;
  return true;
}

Status signIn(const std::string& host, const Login& login, bool remember) {
  const auto name = lowerCase(host);
  hideInRunLog(name, "<smb-host>");
  auto& r = registry();
  std::optional<SavedLogin> previous;
  {
    std::lock_guard lock(r.mutex);
    const auto it = r.logins.find(name);
    if (it != r.logins.end()) previous = it->second;
    r.logins[name].login = login;
  }
  auto c = connection(name);
  const Status status = c->call([&] { c->destroy(); return c->connect("IPC$"); }, true);
  {
    std::lock_guard lock(r.mutex);
    if (status == Status::Ok) {
      r.logins[name].remembered = remember;
      if (remember || (previous && previous->remembered)) r.save();  // else the file is as it was
    } else if (previous) {
      r.logins[name] = *previous;
    } else {
      r.logins.erase(name);
    }
  }
  return status;
}

void forget(const std::string& host) {
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  const auto name = lowerCase(host);
  r.logins.erase(name);
  r.save();
  const auto it = r.connections.find(name);
  if (it != r.connections.end()) it->second->post([c = it->second.get()] { c->destroy(); }, true);
}

std::vector<Server> servers() {
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  std::vector<Server> result;
  for (auto& [host, saved] : r.logins)
    result.push_back({host, saved.name.empty() ? host : saved.name});
  return result;
}

void setName(const std::string& host, const std::string& name) {
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  const auto it = r.logins.find(lowerCase(host));
  if (it == r.logins.end()) return;
  it->second.name = name;
  if (it->second.remembered) r.save();
}

void remember(const std::string& host) {
  auto& r = registry();
  std::lock_guard lock(r.mutex);
  const auto it = r.logins.find(lowerCase(host));
  if (it == r.logins.end()) return;
  it->second.remembered = true;
  r.save();
}

Status list(const std::string& folder, std::vector<Entry>& entries) {
  entries.clear();
  const Location loc(folder);
  auto c = connection(loc.server);
  return c->call([&] {
    Status status = c->connect(loc.share.empty() ? "IPC$" : loc.share);
    if (status != Status::Ok) return status;
    Result r;
    if (loc.share.empty()) {
      const int rc = c->wait(r, smb2_share_enum_async(c->ctx, SHARE_INFO_1, Result::cb, &r));
      if (rc < 0) return statusOf(rc);
      auto* reply = static_cast<srvsvc_NetrShareEnum_rep*>(r.data);
      status = reply->status ? Status::Denied : Status::Ok;
      auto& info = reply->ses.ShareInfo.Level1;
      for (uint32_t i = 0; status == Status::Ok && i < info.EntriesRead; ++i) {
        auto& share = info.Buffer->share_info_1[i];
        const std::string name = share.netname.utf8;
        if ((share.type & 3) == SHARE_TYPE_DISKTREE && !name.empty() && name.back() != '$') {
          hideInRunLog(name, "<smb-share>");
          entries.push_back({name, true});
        }
      }
      smb2_free_data(c->ctx, reply);
      return status;
    }
    const int rc = c->wait(r, smb2_opendir_async(c->ctx, loc.path.c_str(), Result::cb, &r));
    if (rc < 0) return statusOf(rc);
    auto* dir = static_cast<smb2dir*>(r.data);
    while (auto* item = smb2_readdir(c->ctx, dir)) {
      const std::string name = item->name;
      if (name != "." && name != "..") entries.push_back(entry(name, item->st));
    }
    smb2_closedir(c->ctx, dir);
    return Status::Ok;
  });
}

std::vector<std::string> subtitlesBeside(const std::string& locator) {
  const std::string folder = parent(locator);
  const std::string base = foldFileName(std::filesystem::path(locator).stem().string());
  const std::string prefix = base + ".";
  std::vector<Entry> entries;
  std::vector<std::string> paths;
  if (list(folder, entries) != Status::Ok) return paths;
  for (const auto& entry : entries) {
    if (entry.folder || !isSubtitleFile(entry.name)) continue;
    const std::string name = foldFileName(std::filesystem::path(entry.name).stem().string());
    if (name == base || (name.size() > prefix.size() &&
                        name.compare(0, prefix.size(), prefix) == 0))
      paths.push_back(folder + "/" + entry.name);
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

Status stat(const std::string& locator, Entry& out) {
  const Location loc(locator);
  auto c = connection(loc.server);
  return c->call([&] {
    const Status status = c->connect(loc.share.empty() ? "IPC$" : loc.share);
    if (status != Status::Ok) return status;
    smb2_stat_64 st{};
    Result r;
    const int rc = c->wait(r, smb2_stat_async(c->ctx, loc.path.c_str(), &st, Result::cb, &r));
    if (rc >= 0) out = entry(locator.substr(locator.rfind('/') + 1), st);
    return statusOf(rc);
  });
}

Status remove(const std::string& locator, uint64_t size, int64_t modified) {
  const Location loc(locator);
  auto c = connection(loc.server);
  for (int tries = 1;; ++tries) {
    const Status status = c->call([&] { return c->unlink(loc, size, modified); });
    if (status != Status::InUse || tries == 12) return status;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
}

struct File::Impl {
  std::shared_ptr<Connection> connection;
  Location location;
  Handle handle;
  explicit Impl(const std::string& locator)
      : connection(smb::connection(host(locator))), location(locator) {}
};

std::unique_ptr<File> File::open(const std::string& locator, Status& status) {
  auto file = std::unique_ptr<File>(new File);
  file->impl_ = std::make_unique<Impl>(locator);
  auto& p = *file->impl_;
  status = p.connection->call([&] { return p.connection->open(p.location, p.handle); });
  return status == Status::Ok ? std::move(file) : nullptr;
}
File::~File() {
  auto& p = *impl_;
  p.connection->post([c = p.connection.get(), h = p.handle] { c->close(h); });
}
uint64_t File::size() const { return impl_->handle.length; }
int64_t File::read(uint64_t offset, void* out, uint64_t count) {
  auto& p = *impl_;
  return p.connection->call([&]() -> int64_t {
    auto& c = *p.connection;
    if (p.handle.epoch != c.epoch && c.open(p.location, p.handle) != Status::Ok) return -1;
    if (offset >= p.handle.length) return 0;
    c.serviceTree = p.handle.tree;
    smb2_select_tree_id(c.ctx, p.handle.tree);
    const auto n = std::min<uint64_t>({count, p.handle.length - offset, kChunk,
                                      smb2_get_max_read_size(c.ctx)});
    Result r;
    const int rc = c.wait(r, smb2_pread_async(c.ctx, p.handle.fh, static_cast<uint8_t*>(out),
                                            n, offset, Result::cb, &r));
    return rc > 0 ? rc : -1;
  });
}

namespace {
// The next bytes of a stream's window; -1 once it failed or was cancelled.
int64_t readStream(Connection& c, Stream& s, void* out, uint64_t count) {
  std::unique_lock lock(s.mutex);
  while (!s.cancelled && !s.failed) {
    if (s.position >= s.handle.length) return 0;
    if (const size_t n = s.take(out, count)) {
      c.notify();
      return n;
    }
    s.cv.wait_for(lock, std::chrono::milliseconds(50));
  }
  return -1;
}
}  // namespace

struct Reader::Impl {
  std::shared_ptr<Connection> connection;
  std::shared_ptr<Stream> stream = std::make_shared<Stream>();
};

std::unique_ptr<Reader> Reader::open(const std::string& locator, uint64_t offset, Status& status) {
  auto reader = std::unique_ptr<Reader>(new Reader);
  reader->impl_ = std::make_unique<Impl>();
  auto& p = *reader->impl_;
  p.connection = connection(host(locator));
  auto& s = *p.stream;
  s.locator = locator;
  s.background = true;
  s.position = offset;
  auto& c = *p.connection;
  status = c.call([&] {
    const Status status = c.open(Location(locator), s.handle);
    if (status == Status::Ok) c.streams.push_back(p.stream);
    return status;
  });
  return status == Status::Ok ? std::move(reader) : nullptr;
}
Reader::~Reader() { cancel(); }
uint64_t Reader::size() const {
  std::lock_guard lock(impl_->stream->mutex);
  return impl_->stream->handle.length;
}
int64_t Reader::read(void* out, uint64_t count) {
  return readStream(*impl_->connection, *impl_->stream, out, count);
}
void Reader::cancel() {
  impl_->stream->cancelled = true;
  impl_->stream->cv.notify_all();
  impl_->connection->notify();
}

namespace {
int backgroundReader;
struct MpvStream {
  std::shared_ptr<Connection> connection;
  std::shared_ptr<Stream> stream;
  std::unique_ptr<File> file;  // preview: one background positional read, no read-ahead
  uint64_t position = 0;
  std::atomic<bool> cancelled{false};
  std::atomic<bool>* failed = nullptr;
};
int openMpv(void* data, char* uri, mpv_stream_cb_info* info) {
  const std::string url = uri;
  const std::string locator = "smb://" + url.substr(url.find("://") + 3);
  auto p = std::make_unique<MpvStream>();
  p->connection = connection(host(locator));
  if (data == &backgroundReader) {
    Status status;
    p->file = File::open(locator, status);
    if (!p->file) return MPV_ERROR_LOADING_FAILED;
  } else {
    p->failed = static_cast<std::atomic<bool>*>(data);
    if (p->failed) *p->failed = false;
    p->stream = std::make_shared<Stream>();
    p->stream->locator = locator;
    auto& c = *p->connection;
    const Status status = c.call([&] {
      const Status status = c.open(Location(locator), p->stream->handle);
      if (status == Status::Ok) c.streams.push_back(p->stream);
      return status;
    }, true);
    if (status != Status::Ok) return MPV_ERROR_LOADING_FAILED;
  }
  info->cookie = p.release();
  info->read_fn = [](void* cookie, char* out, uint64_t count) -> int64_t {
    auto& p = *static_cast<MpvStream*>(cookie);
    if (p.cancelled) return -1;
    if (p.file) {
      const int64_t n = p.file->read(p.position, out, count);
      if (n > 0) p.position += n;
      return n;
    }
    auto& s = *p.stream;
    // FFmpeg can ask for its whole grown buffer (15 MB) at a time; on a file
    // whose audio is stored apart from its video it then takes one packet and
    // jumps away, so a read is cut to mpv's own buffer size.
    const int64_t n = readStream(*p.connection, s, out, std::min<uint64_t>(count, 128 * 1024));
    if (n < 0 && s.failed && !s.cancelled && p.failed) *p.failed = true;
    return n;
  };
  info->seek_fn = [](void* cookie, int64_t offset) -> int64_t {
    auto& p = *static_cast<MpvStream*>(cookie);
    if (offset < 0 || p.cancelled) return -1;
    if (p.file) { p.position = offset; return offset; }
    auto& s = *p.stream;
    std::lock_guard lock(s.mutex);
    if (s.failed) return -1;
    if (uint64_t(offset) != s.position) {
      s.seek(offset);
      p.connection->notify();
    }
    return offset;
  };
  info->size_fn = [](void* cookie) -> int64_t {
    auto& p = *static_cast<MpvStream*>(cookie);
    if (p.file) return p.file->size();
    std::lock_guard lock(p.stream->mutex);
    return p.stream->handle.length;
  };
  info->cancel_fn = [](void* cookie) {
    auto& p = *static_cast<MpvStream*>(cookie);
    p.cancelled = true;
    if (p.stream) { p.stream->cancelled = true; p.stream->cv.notify_all(); }
    p.connection->notify();
  };
  info->close_fn = [](void* cookie) {
    auto* p = static_cast<MpvStream*>(cookie);
    if (p->stream) { p->stream->cancelled = true; p->connection->notify(); }
    delete p;  // the service thread retires pending reads and closes the handle
  };
  return 0;
}
}  // namespace

void addTo(mpv_handle* mpv, bool background, std::atomic<bool>* failed) {
  mpv_stream_cb_add_ro(mpv, "smbfile",
                       background ? static_cast<void*>(&backgroundReader) : failed, openMpv);
  mpv_stream_cb_add_ro(mpv, "smbread", &backgroundReader, openMpv);
}
std::string mpvUrl(const std::string& locator, bool background) {
  if (!isSmb(locator)) return locator;
  return std::string(background ? "smbread://" : "smbfile://") + locator.substr(6);
}
}  // namespace smb
