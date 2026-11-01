#pragma once

#include <Windows.h>

namespace CoreDiagnostics {
#ifdef R3NZ_DIAGNOSTICS
// 诊断构建专用：使用固定长度缓冲区，不记录地址、账号或游戏内容。
inline char* append_number(char* out, unsigned long long value) noexcept
{
    char digits[20];
    unsigned count = 0;
    do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    while (count) *out++ = digits[--count];
    return out;
}

inline bool log_path(wchar_t (&path)[MAX_PATH]) noexcept
{
    const DWORD size = ::GetTempPathW(MAX_PATH, path);
    if (!size || size + 40 >= MAX_PATH) return false;
    wchar_t* out = path + size;
    for (const wchar_t* p = L"R3nzSkin-core-"; *p; ++p) *out++ = *p;
    char pid[20];
    const auto end = append_number(pid, ::GetCurrentProcessId());
    for (auto p = pid; p != end; ++p) *out++ = *p;
    for (const wchar_t* p = L".log"; *p; ++p) *out++ = *p;
    *out = 0;
    return true;
}

inline void record(const char* stage, DWORD code = 0) noexcept
{
    // 保留调用方错误码；失败的日志写入不得改变核心流程。
    const DWORD savedError = ::GetLastError();
    wchar_t path[MAX_PATH]{};
    if (log_path(path)) {
        const HANDLE file = ::CreateFileW(path, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            char line[256];
            char* out = line;
            auto text = [&](const char* value) { while (*value && out < line + 190) *out++ = *value++; };
            text("[R3NZ-CORE] pid="); out = append_number(out, ::GetCurrentProcessId());
            text(" tid="); out = append_number(out, ::GetCurrentThreadId());
            text(" tick="); out = append_number(out, ::GetTickCount64());
            text(" stage="); text(stage);
            text(" code="); out = append_number(out, code);
            *out++ = '\n';
            DWORD written{};
            ::WriteFile(file, line, static_cast<DWORD>(out - line), &written, nullptr);
            ::CloseHandle(file);
        }
    }
    ::SetLastError(savedError);
}
#else
inline void record(const char*, DWORD = 0) noexcept {}
#endif
}
