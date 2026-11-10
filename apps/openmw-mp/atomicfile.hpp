#ifndef ARENAMP_ATOMICFILE_HPP
#define ARENAMP_ATOMICFILE_HPP

#include <string>
#include <string_view>
#include <cstdio>
#include <array>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <limits>
#else
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ArenaFileOps
{
    inline bool validPath(const std::string& path) noexcept
    {
        return !path.empty() && path.find('\0') == std::string::npos;
    }
#ifdef _WIN32
    inline bool widePath(const std::string& path, std::wstring& result)
    {
        if (!validPath(path) || path.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return false;
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            path.data(), static_cast<int>(path.size()), nullptr, 0);
        if (!count) return false;
        result.resize(count);
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
            static_cast<int>(path.size()), &result[0], count) == count;
    }
#endif
    // Use UTF-16 paths on Windows; Lua's narrow fopen cannot open Cyrillic names.
    inline FILE* open(const std::string& path, bool write)
    {
        if (!validPath(path)) return nullptr;
#ifdef _WIN32
        std::wstring name;
        if (!widePath(path, name)) return nullptr;
        return _wfopen(name.c_str(), write ? L"wb" : L"rb");
#else
        return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
    }
    inline bool read(const std::string& path, std::string& result) noexcept
    {
        FILE* file = nullptr;
        try
        {
            file = open(path, false);
            if (!file) return false;
            std::string staged;
            std::array<char, 16384> buffer;
            std::size_t n;
            while ((n = std::fread(buffer.data(), 1, buffer.size(), file)) != 0)
            {
                if (n > 64u*1024u*1024u - staged.size()) { std::fclose(file); return false; }
                staged.append(buffer.data(), n);
            }
            const bool ok = std::ferror(file) == 0;
            const bool closed = std::fclose(file) == 0; file = nullptr;
            if (!ok || !closed) return false;
            result.swap(staged); return true;
        }
        catch (...) { if (file) std::fclose(file); return false; }
    }
    inline bool write(const std::string& path, std::string_view content) noexcept
    {
        if (content.size()>64u*1024u*1024u) return false;
        FILE* file = nullptr;
        try
        {
            file = open(path, true);
            if (!file) return false;
            const bool written = std::fwrite(content.data(), 1, content.size(), file) == content.size();
            const bool flushed = std::fflush(file) == 0;
            const bool closed = std::fclose(file) == 0; file = nullptr;
            return written && flushed && closed;
        }
        catch (...) { if (file) std::fclose(file); return false; }
    }
    // Profile migration must never replace an already existing named account.
    inline bool renameNoReplace(const std::string& source, const std::string& destination) noexcept
    {
        if (!validPath(source) || !validPath(destination)) return false;
#ifdef _WIN32
        try
        {
            std::wstring from, to;
            return widePath(source, from) && widePath(destination, to)
                && MoveFileExW(from.c_str(), to.c_str(), 0) != 0;
        }
        catch (...) { return false; }
#else
        if (::link(source.c_str(), destination.c_str()) != 0) return false;
        // If unlink fails, both links contain the same intact account.
        return ::unlink(source.c_str()) == 0;
#endif
    }
    inline bool remove(const std::string& path) noexcept
    {
        if (!validPath(path)) return false;
#ifdef _WIN32
        try { std::wstring name; return widePath(path,name) && DeleteFileW(name.c_str()) != 0; }
        catch (...) { return false; }
#else
        return ::unlink(path.c_str()) == 0;
#endif
    }
    // The temporary file must be beside the destination. Never delete the old
    // file first or fall back to copy: journal commits require atomic replacement.
    // POSIX APIs avoid the separate filesystem runtime missing in older NDK/libc++.
    inline bool replace(const std::string& source, const std::string& destination) noexcept
    {
        if (!validPath(source) || !validPath(destination)) return false;
#ifdef _WIN32
        try
        {
            std::wstring from, to;
            if (!widePath(source, from) || !widePath(destination, to)) return false;
            return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        }
        catch (...) { return false; }
#else
        return ::rename(source.c_str(), destination.c_str()) == 0;
#endif
    }
    // 1 = exists, 0 = missing, -1 = inaccessible/invalid. Permission failures
    // must not be treated as an empty database by the persistence code.
    inline int status(const std::string& path) noexcept
    {
        if (!validPath(path)) return -1;
#ifdef _WIN32
        try
        {
            std::wstring name;
            if (!widePath(path, name)) return -1;
            if (GetFileAttributesW(name.c_str()) != INVALID_FILE_ATTRIBUTES) return 1;
            const DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
        }
        catch (...) { return -1; }
#else
        struct stat info;
        if (::stat(path.c_str(), &info) == 0) return 1;
        return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
#endif
    }
}
#endif
