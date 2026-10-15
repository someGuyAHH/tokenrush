#include "platform.hpp"

#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::filesystem::path exeDir() {
    std::error_code ec;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (n > 0 && n < sizeof buf / sizeof buf[0]) return std::filesystem::path(buf).parent_path();
#else
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);   // Linux; fails elsewhere
    if (!ec) return p.parent_path();
#endif
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}
