// The file half of hal::sd -- POSIX calls, one copy for both backends.   [FIRMWARE.md §6.3]
//
// The target mounts FATFS into the VFS at /sd, so open()/read()/opendir() ARE the card; the
// host points the same calls at a directory.  What differs -- mounting, and where /sd is -- is
// in each backend's sd::detail::native_path().  Everything that can be the same code is,
// which is what makes the host's listing, its short reads and its missing-file answers the
// target's (§11.2).
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "clk/hal/hal.hpp"
#include "clk/port.hpp"
#include "sd_detail.hpp"

namespace clk::hal::sd {
namespace {

constexpr std::size_t kPathMax = 192;

port::Mutex g_fd_mx;
int g_fds[kMaxOpen] = {-1, -1, -1};

int native_fd(int h) noexcept {
    if (h < 0 || h >= kMaxOpen) return -1;
    port::Lock lk{g_fd_mx};
    return g_fds[h];
}

}  // namespace

Status list(const char* dir, ListFn fn, void* ctx) noexcept {
    if (!dir || !fn) return Status::BadArg;
    char path[kPathMax];
    if (const Status st = detail::native_path(dir, path, sizeof path); st != Status::Ok) return st;
    DIR* d = ::opendir(path);
    if (!d) return Status::Failed;
    while (const dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        // A name that does not fit is skipped rather than shown cut short: a truncated name
        // is one nobody could then open, select or play.
        Entry out{};
        const int nl = std::snprintf(out.name, sizeof out.name, "%s", e->d_name);
        if (nl < 0 || static_cast<std::size_t>(nl) >= sizeof out.name) continue;
        char full[kPathMax + kNameMax];
        const int fl = std::snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (fl < 0 || static_cast<std::size_t>(fl) >= sizeof full) continue;
        struct stat st{};
        if (::stat(full, &st) == 0) {
            out.dir = S_ISDIR(st.st_mode);
            out.size = out.dir ? 0u : static_cast<uint32_t>(st.st_size);
        }
        if (!fn(out, ctx)) break;
    }
    ::closedir(d);
    return Status::Ok;
}

Result<int> open(const char* path) noexcept {
    if (!path) return Result<int>::bad(Status::BadArg);
    char p[kPathMax];
    if (const Status st = detail::native_path(path, p, sizeof p); st != Status::Ok)
        return Result<int>::bad(st);
    const int fd = ::open(p, O_RDONLY);
    if (fd < 0) return Result<int>::bad(Status::Failed);
    port::Lock lk{g_fd_mx};
    for (int h = 0; h < kMaxOpen; ++h) {
        if (g_fds[h] < 0) {
            g_fds[h] = fd;
            return Result<int>::good(h);
        }
    }
    ::close(fd);
    return Result<int>::bad(Status::Busy);
}

Result<std::size_t> read(int h, void* buf, std::size_t n) noexcept {
    const int fd = native_fd(h);
    if (fd < 0 || !buf) return Result<std::size_t>::bad(Status::BadArg);
    const auto r = ::read(fd, buf, n);
    // A card pulled mid-stream is an EIO here, and it is the one failure `storage` has to
    // tell apart from the end of the file: 0 is "no more", Failed is "the card went away".
    if (r < 0) return Result<std::size_t>::bad(Status::Failed);
    return Result<std::size_t>::good(static_cast<std::size_t>(r));
}

Status seek(int h, uint32_t off) noexcept {
    const int fd = native_fd(h);
    if (fd < 0) return Status::BadArg;
    return ::lseek(fd, static_cast<off_t>(off), SEEK_SET) < 0 ? Status::Failed : Status::Ok;
}

Result<uint32_t> size(int h) noexcept {
    const int fd = native_fd(h);
    if (fd < 0) return Result<uint32_t>::bad(Status::BadArg);
    struct stat st{};
    if (::fstat(fd, &st) != 0) return Result<uint32_t>::bad(Status::Failed);
    return Result<uint32_t>::good(static_cast<uint32_t>(st.st_size));
}

Result<int> create(const char* path, bool append) noexcept {
    if (!path) return Result<int>::bad(Status::BadArg);
    char p[kPathMax];
    if (const Status st = detail::native_path(path, p, sizeof p); st != Status::Ok)
        return Result<int>::bad(st);
    const int fd = ::open(p, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) return Result<int>::bad(Status::Failed);
    port::Lock lk{g_fd_mx};
    for (int h = 0; h < kMaxOpen; ++h) {
        if (g_fds[h] < 0) {
            g_fds[h] = fd;
            return Result<int>::good(h);
        }
    }
    ::close(fd);
    return Result<int>::bad(Status::Busy);
}

Result<std::size_t> write(int h, const void* buf, std::size_t n) noexcept {
    const int fd = native_fd(h);
    if (fd < 0 || !buf) return Result<std::size_t>::bad(Status::BadArg);
    const auto r = ::write(fd, buf, n);
    if (r < 0) return Result<std::size_t>::bad(Status::Failed);
    return Result<std::size_t>::good(static_cast<std::size_t>(r));
}

Status remove(const char* path) noexcept {
    char p[kPathMax];
    if (const Status st = detail::native_path(path, p, sizeof p); st != Status::Ok) return st;
    return ::unlink(p) == 0 ? Status::Ok : Status::Failed;
}

Status rename(const char* from, const char* to) noexcept {
    char a[kPathMax], b[kPathMax];
    if (const Status st = detail::native_path(from, a, sizeof a); st != Status::Ok) return st;
    if (const Status st = detail::native_path(to, b, sizeof b); st != Status::Ok) return st;
    return ::rename(a, b) == 0 ? Status::Ok : Status::Failed;
}

Status mkdir(const char* path) noexcept {
    char p[kPathMax];
    if (const Status st = detail::native_path(path, p, sizeof p); st != Status::Ok) return st;
    struct stat st{};
    if (::stat(p, &st) == 0) return S_ISDIR(st.st_mode) ? Status::Ok : Status::Failed;
    return ::mkdir(p, 0755) == 0 ? Status::Ok : Status::Failed;
}

void close(int h) noexcept {
    if (h < 0 || h >= kMaxOpen) return;
    int fd = -1;
    {
        port::Lock lk{g_fd_mx};
        fd = g_fds[h];
        g_fds[h] = -1;
    }
    if (fd >= 0) ::close(fd);
}

}  // namespace clk::hal::sd
