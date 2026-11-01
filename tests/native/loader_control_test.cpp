#include <Windows.h>
#include <cstdio>
#include <string>
#include "../../R3nzSkin_Injector/Injector.hpp"
#include "../../R3nzSkin_Injector/InjectorState.hpp"

struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
};

int main()
{
    wchar_t executable[MAX_PATH]{};
    if (!::GetModuleFileNameW(nullptr, executable, MAX_PATH)) return 2;
    const std::wstring fullPath(executable);
    const auto directory = fullPath.substr(0, fullPath.find_last_of(L"\\/"));
    if (!::SetCurrentDirectoryW(directory.c_str())) return 3;
    // 只映射检查导出，不执行 DLL 入口；拒绝误放进来的真实核心 DLL。
    const HMODULE fixture = ::LoadLibraryExW(L"R3nzSkin.dll", nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!fixture) return 4;
    const bool isFixture = ::GetProcAddress(fixture, "R3nzFixtureMarker") != nullptr;
    ::FreeLibrary(fixture);
    if (!isFixture) return 5;

    Handle job{ ::CreateJobObjectW(nullptr, nullptr) };
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !::SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return 6;

    const auto host = directory + L"\\LoaderFixtureHost.exe";
    std::wstring command = L"\"" + host + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!::CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &info)) return 7;
    Handle child{info.hProcess}, thread{info.hThread};
    if (!::AssignProcessToJobObject(job.value, child.value)) {
        ::TerminateProcess(child.value, 8);
        return 8;
    }
    const auto eventName = L"Local\\R3nzLoaderFixture-" + std::to_wstring(info.dwProcessId);
    Handle entered{ ::CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()) };
    const auto readyName = L"Local\\R3nzLoaderReady-" + std::to_wstring(info.dwProcessId);
    Handle ready{ ::CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()) };
    if (!entered.value || !ready.value || ::ResumeThread(thread.value) == static_cast<DWORD>(-1)) return 9;
    if (::WaitForSingleObject(ready.value, 5000) != WAIT_OBJECT_0) return 10;

    // 不接收外部 PID；只对本次创建且受 Job 管理的空进程执行原有代码。
    const auto before = Injector::probeModule(info.dwProcessId);
    const bool returned = Injector::inject(info.dwProcessId);
    const bool entryObserved = ::WaitForSingleObject(entered.value, 1000) == WAIT_OBJECT_0;
    const auto after = Injector::probeModule(info.dwProcessId);
    const auto diagnostic = R3nzSkinInjector::unpack_diagnostic(R3nzSkinInjector::loadDiagnostic.load());
    std::printf("CONTROL pid=%lu before=%d returned=%d dll_entry=%d after=%d load=%s\n",
        info.dwProcessId, static_cast<int>(before), returned, entryObserved, static_cast<int>(after),
        R3nzSkinInjector::format_diagnostic(diagnostic).c_str());
    if (before != R3nzSkinInjector::ModuleProbe::Absent || !returned || !entryObserved || after != R3nzSkinInjector::ModuleProbe::Present) return 1;
    std::puts("PASS: unchanged Injector.cpp loads fixture into owned host and confirms target DLL entry");
    return 0;
}
