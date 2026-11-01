#include <Windows.h>
#include <cwchar>

extern "C" __declspec(dllexport) void R3nzFixtureMarker() {}

BOOL WINAPI DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t name[80];
        swprintf_s(name, L"Local\\R3nzLoaderFixture-%lu", ::GetCurrentProcessId());
        const HANDLE event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
        if (event) { ::SetEvent(event); ::CloseHandle(event); }
    }
    return TRUE;
}
