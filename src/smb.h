#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct mpv_handle;

// In-app SMB (libsmb2): browsing and reading files on SMB2/3 shares without
// an OS mount, on the desktop and the Steam Frame. Read-only, but for
// deleting the playing file (remove()).
//
// A share's file or folder is named by a locator, smb://host/share/path:
// raw UTF-8 bytes, '/' between parts, never percent-encoded and never with
// credentials. The rest of the app keeps a locator as it keeps a local path
// (library path cache, pins, history, folders.conf). smb://host lists the
// host's shares. host is lowercase: an address, or a name as typed.
//
// Everything here may block on the network, bounded by timeouts: call it from
// workers (or mpv's and FFmpeg's own threads), never from the render loop.
// One connection per server, shared by all its users; playback's reads go
// first.
namespace smb {

bool isSmb(std::string_view location);
// smb://h/s/a -> smb://h/s -> smb://h; "" above smb://h.
std::string parent(const std::string& locator);
// "h" of smb://h/..., "" if not a locator.
std::string host(const std::string& locator);

enum class Status {
  Ok,
  Missing,     // no such file, folder or share: the only one that may drop library paths
  Denied,      // signed in, but not allowed (a share list refused, a folder off limits)
  NeedsLogin,  // no password known for the server this run, or the server refused it
  Offline,     // no answer, timed out, or the connection broke
  InUse,       // remove(): open elsewhere without letting it be deleted (another device)
  Changed,     // remove(): not a file of that size and date (any more)
};

struct Entry {
  std::string name;
  bool folder = false;
  uint64_t size = 0;
  int64_t modified = 0;  // seconds since 1970
};
// smb://host: its shares (as folders; hidden "$" shares left out).
// smb://host/share[/path]: that folder's entries.
Status list(const std::string& folder, std::vector<Entry>& entries);
Status stat(const std::string& locator, Entry& entry);
// Deletes one file for good (docs/alpha/delete-file.md), never a folder and
// never what a link points to, if it is still `size` bytes dated
// `modified`. Our own handles on it close by themselves after use: while
// one is open the server refuses (InUse), which is tried again for a few
// seconds.
Status remove(const std::string& locator, uint64_t size, int64_t modified);
std::vector<std::string> subtitlesBeside(const std::string& locator);

// A file read at any position, apart from playback (oshash, thumbnails,
// photos). Background priority: it never delays playback's reads.
class File {
 public:
  static std::unique_ptr<File> open(const std::string& locator, Status& status);
  ~File();
  uint64_t size() const;
  // Up to `count` bytes at `offset`; 0 only at the end of the file, -1 on error.
  int64_t read(uint64_t offset, void* out, uint64_t count);

 private:
  File() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// A file read from `offset` to its end (an offline download): two 1 MiB
// reads in flight, as playback's, but only while no video plays from the
// server. Reconnects as playback does.
class Reader {
 public:
  static std::unique_ptr<Reader> open(const std::string& locator, uint64_t offset, Status& status);
  ~Reader();
  uint64_t size() const;
  // The next bytes, up to `count`; 0 at the end, -1 on error or once cancelled.
  int64_t read(void* out, uint64_t count);
  void cancel();  // any thread: a read waiting returns -1

 private:
  Reader() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Signing in. An empty user is a guest; "DOMAIN\user" names a domain.
struct Login {
  std::string user, password;
};
// Signs in to `host` with `login`. Ok: kept for this run, and saved if
// `remember` (a file of its own, mode 0600, never in settings or logs).
Status signIn(const std::string& host, const Login& login, bool remember);
// The login known for `host` (saved or this run's), if any. Main thread too:
// no network.
bool knownLogin(const std::string& host, Login& login, bool& remembered);
// Saves `host`'s login of this run, as signIn() with `remember` would have
// (first-run setup under --first-run: only once it ends).
void remember(const std::string& host);
// Forgets `host`'s login, saved and this run's, and closes its connection.
void forget(const std::string& host);

// Servers the user signed in to, saved or this run's, with the name to show
// (the discovered name, else the host). Main thread too: no network.
struct Server {
  std::string host, name;
};
std::vector<Server> servers();
// The name to show for `host` (e.g. from discovery), kept with its login.
void setName(const std::string& host, const std::string& name);

// mpv: registers the stream protocol for locators on this instance (call
// once after mpv_initialize), and the URL to load a locator with.
// background: seek previews share the single background-request budget.
// failed: optional read-error flag, kept alive until mpv is destroyed.
void addTo(mpv_handle* mpv, bool background = false, std::atomic<bool>* failed = nullptr);
std::string mpvUrl(const std::string& locator, bool background = false);

}  // namespace smb
