#include <Windows.h>
#include <cwchar>

int main()
{
    // 仅供对照测试的空进程，不使用游戏进程名或读取游戏数据。
    wchar_t name[80];
    swprintf_s(name, L"Local\\R3nzLoaderReady-%lu", ::GetCurrentProcessId());
    const HANDLE ready = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!ready) return 1;
    ::SetEvent(ready);
    ::CloseHandle(ready);
    ::Sleep(45000);
    return 0;
}
