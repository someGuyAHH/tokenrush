#include "platform.hpp"

#include <csignal>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <string>
#endif

std::filesystem::path exeDir() {
    std::error_code ec;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (n > 0 && n < sizeof buf / sizeof buf[0]) return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);   // fails, but sets `size` to what's needed
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        auto p = std::filesystem::weakly_canonical(buf.c_str(), ec);
        if (!ec) return p.parent_path();
    }
#else
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);   // Linux; fails elsewhere
    if (!ec) return p.parent_path();
#endif
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

namespace {
volatile std::sig_atomic_t g_hangup = 0;
void onHangup(int) { g_hangup = 1; }
}  // namespace

void catchHangup() {
    std::signal(SIGTERM, onHangup);
#ifndef _WIN32
    std::signal(SIGHUP, onHangup);
#endif
}

bool hangupReceived() { return g_hangup != 0; }
