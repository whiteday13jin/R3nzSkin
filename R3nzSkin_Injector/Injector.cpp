#include <Windows.h>
#include <cstdlib>
#include <fstream>
#include <psapi.h>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <regex>
#include <msclr/marshal_cppstd.h>
#include <cerrno>
#include <cstdio>
#include <cwchar>

#include "Injector.hpp"
#include "R3nzUI.hpp"
#include "xorstr.hpp"
#include "lazy_importer.hpp"

using namespace System;
using namespace System::Windows::Forms;
using namespace System::Threading;
using namespace System::Globalization;
using namespace System::Net;

#define xor_clrstr_w(x) msclr::interop::marshal_as<String^>(static_cast<std::wstring>(_XorStrW(x)))

namespace {
    using namespace R3nzSkinInjector;

    void record_diagnostic(const bool probe, const Diagnostic value, const DWORD pid) noexcept
    {
        const auto packed{ pack_diagnostic(value) };
        (probe ? probeDiagnostic : loadDiagnostic).store(packed);
        // 临时诊断只记录失败、阻塞等待和终态，不记录每次轮询的全部数据。
        if (value.outcome == DiagnosticOutcome::Pending && value.stage != DiagnosticStage::WaitThread)
            return;
        static std::uint64_t lastLoad{ UINT64_MAX }, lastProbe{ UINT64_MAX };
        static DWORD lastLoadPid{}, lastProbePid{};
        auto& last{ probe ? lastProbe : lastLoad };
        auto& lastPid{ probe ? lastProbePid : lastLoadPid };
        if (last == packed && lastPid == pid)
            return;
        last = packed;
        lastPid = pid;

        wchar_t path[MAX_PATH]{};
        const auto length{ ::GetModuleFileNameW(nullptr, path, MAX_PATH) };
        if (!length || length >= MAX_PATH)
            return;
        auto slash{ std::wcsrchr(path, L'\\') };
        if (!slash || wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"InjectorDiagnostics.log") != 0)
            return;
        FILE* file{};
        if (_wfopen_s(&file, path, L"a") != 0 || !file)
            return;
        SYSTEMTIME now{};
        ::GetSystemTime(&now);
        std::fprintf(file, "[DEBUG-r3diag] %04u-%02u-%02uT%02u:%02u:%02uZ pid=%lu channel=%s stage=%s outcome=%s domain=%s code=%u hex=0x%08X\n",
            static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay),
            static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
            static_cast<unsigned long>(pid), probe ? "probe" : "load",
            diagnostic_stage_name(value.stage), diagnostic_outcome_name(value.outcome),
            diagnostic_domain_name(value.domain), static_cast<unsigned>(value.code), static_cast<unsigned>(value.code));
        std::fclose(file);
    }
}

proclist_t WINAPI Injector::findProcesses(const std::wstring& name) noexcept
{
	auto process_snap{ LI_FN(CreateToolhelp32Snapshot)(TH32CS_SNAPPROCESS, 0) };
	proclist_t list;

	if (process_snap == INVALID_HANDLE_VALUE)
		return list;

	PROCESSENTRY32W pe32{};
	pe32.dwSize = sizeof(PROCESSENTRY32W);

	if (LI_FN(Process32FirstW).get()(process_snap, &pe32)) {
		if (pe32.szExeFile == name)
			list.push_back(pe32.th32ProcessID);

		while (LI_FN(Process32NextW).get()(process_snap, &pe32)) {
			if (pe32.szExeFile == name)
				list.push_back(pe32.th32ProcessID);
		}
	}

	LI_FN(CloseHandle)(process_snap);
	return list;
}

R3nzSkinInjector::ModuleProbe WINAPI Injector::probeModule(const std::uint32_t pid) noexcept
{
	auto hProcess{ LI_FN(OpenProcess)(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid) };

	if (nullptr == hProcess) {
		record_diagnostic(true, { DiagnosticStage::OpenProbe, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
		return ModuleProbe::Unavailable;
	}

	HMODULE hMods[1024];
	DWORD cbNeeded{};

	if (LI_FN(K32EnumProcessModules)(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
		// cbNeeded 是所需总容量，可能超过实际写入的固定数组，不能越界读取。
		if (cbNeeded > sizeof(hMods)) {
			record_diagnostic(true, { DiagnosticStage::EnumModules, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ERROR_INSUFFICIENT_BUFFER }, pid);
			LI_FN(CloseHandle)(hProcess);
			return ModuleProbe::Unavailable;
		}
		for (auto i{ 0u }; i < (cbNeeded / sizeof(HMODULE)); ++i) {
			TCHAR szModName[MAX_PATH];
			if (LI_FN(K32GetModuleBaseNameW)(hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR))) {
				if (std::wcscmp(szModName, _XorStrW(L"R3nzSkin.dll")) == 0) {
					record_diagnostic(true, { DiagnosticStage::ModulePresent, DiagnosticOutcome::Completed }, pid);
					LI_FN(CloseHandle)(hProcess);
					return ModuleProbe::Present;
				}
			} else {
				record_diagnostic(true, { DiagnosticStage::ModuleName, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
				LI_FN(CloseHandle)(hProcess);
				return ModuleProbe::Unavailable;
			}
		}
		record_diagnostic(true, { DiagnosticStage::ModuleAbsent, DiagnosticOutcome::NotObserved }, pid);
	} else {
		record_diagnostic(true, { DiagnosticStage::EnumModules, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
		LI_FN(CloseHandle)(hProcess);
		return ModuleProbe::Unavailable;
	}
	LI_FN(CloseHandle)(hProcess);
	return ModuleProbe::Absent;
}

bool WINAPI Injector::inject(const std::uint32_t pid) noexcept
{
	TCHAR current_dir[MAX_PATH]{};
	const auto directoryLength{ LI_FN(GetCurrentDirectoryW)(MAX_PATH, current_dir) };
	if (!directoryLength || directoryLength >= MAX_PATH) {
		record_diagnostic(false, { DiagnosticStage::GetDirectory, DiagnosticOutcome::Failed, DiagnosticDomain::Win32,
			directoryLength ? ERROR_INSUFFICIENT_BUFFER : ::GetLastError() }, pid);
		return false;
	}
	record_diagnostic(false, { DiagnosticStage::OpenTarget }, pid);
	const auto handle{ LI_FN(OpenProcess)(PROCESS_ALL_ACCESS, false, pid) };

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		record_diagnostic(false, { DiagnosticStage::OpenTarget, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
		return false;
	}

	FILETIME ft{};
	SYSTEMTIME st{};
	LI_FN(GetSystemTime)(&st);
	LI_FN(SystemTimeToFileTime)(&st, &ft);
	FILETIME create{}, exit{}, kernel{}, user{};
	LI_FN(GetProcessTimes)(handle, &create, &exit, &kernel, &user);

	const auto delta{ 10 - static_cast<std::int32_t>((*reinterpret_cast<std::uint64_t*>(&ft) - *reinterpret_cast<std::uint64_t*>(&create.dwLowDateTime)) / 10000000U) };

	if (delta > 0)
		std::this_thread::sleep_for(std::chrono::seconds(delta));

	const auto dll_path{ std::wstring(current_dir) + _XorStrW(L"\\R3nzSkin.dll") };

	if (const auto f{ std::ifstream(dll_path) }; !f.is_open()) {
		record_diagnostic(false, { DiagnosticStage::DllFile, DiagnosticOutcome::Failed, DiagnosticDomain::Crt, static_cast<std::uint32_t>(errno) }, pid);
		LI_FN(MessageBoxW)(nullptr, _XorStrW(L"R3nzSkin.dll file could not be found.\nTry reinstalling the cheat."), _XorStrW(L"R3nzSkin"), MB_ICONERROR | MB_OK);
		LI_FN(CloseHandle)(handle);
		return false;
	}

	record_diagnostic(false, { DiagnosticStage::AllocatePath }, pid);
	const auto dll_path_remote{ LI_FN(VirtualAllocEx).get()(handle, nullptr, (dll_path.size() + 1) * sizeof(wchar_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) };

	if (!dll_path_remote) {
		record_diagnostic(false, { DiagnosticStage::AllocatePath, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
		LI_FN(CloseHandle)(handle);
		return false;
	}

	record_diagnostic(false, { DiagnosticStage::WritePath }, pid);
	if (!LI_FN(WriteProcessMemory).get()(handle, dll_path_remote, dll_path.data(), (dll_path.size() + 1) * sizeof(wchar_t), nullptr)) {
		record_diagnostic(false, { DiagnosticStage::WritePath, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
		LI_FN(VirtualFreeEx).get()(handle, dll_path_remote, 0u, MEM_RELEASE);
		LI_FN(CloseHandle)(handle);
		return false;
	}

	HANDLE thread{};
	record_diagnostic(false, { DiagnosticStage::CreateThread }, pid);
	const auto threadStatus{ LI_FN(NtCreateThreadEx).nt_cached()(&thread, GENERIC_ALL, nullptr, handle, reinterpret_cast<LPTHREAD_START_ROUTINE>(LI_FN(GetProcAddress).get()(LI_FN(GetModuleHandleW).get()(_XorStrW(L"kernel32.dll")), _XorStr("LoadLibraryW"))), dll_path_remote, FALSE, NULL, NULL, NULL, nullptr) };

	if (!thread || thread == INVALID_HANDLE_VALUE) {
		record_diagnostic(false, { DiagnosticStage::CreateThread, DiagnosticOutcome::Failed, DiagnosticDomain::NtStatus, static_cast<std::uint32_t>(threadStatus) }, pid);
		LI_FN(VirtualFreeEx).get()(handle, dll_path_remote, 0u, MEM_RELEASE);
		LI_FN(CloseHandle)(handle);
		return false;
	}

	record_diagnostic(false, { DiagnosticStage::WaitThread }, pid);
	const auto waitResult{ LI_FN(WaitForSingleObject)(thread, INFINITE) };
	bool completed{ false };
	if (waitResult == WAIT_FAILED) {
		record_diagnostic(false, { DiagnosticStage::WaitThread, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
	} else {
		DWORD exitCode{};
		if (::GetExitCodeThread(thread, &exitCode)) {
			record_diagnostic(false, { DiagnosticStage::ThreadExit, DiagnosticOutcome::Completed, DiagnosticDomain::RawExit, exitCode }, pid);
			completed = waitResult == WAIT_OBJECT_0;
		} else
			record_diagnostic(false, { DiagnosticStage::ThreadExit, DiagnosticOutcome::Failed, DiagnosticDomain::Win32, ::GetLastError() }, pid);
	}
	LI_FN(CloseHandle)(thread);
	LI_FN(VirtualFreeEx).get()(handle, dll_path_remote, 0u, MEM_RELEASE);
	LI_FN(CloseHandle)(handle);
	return completed;
}

void WINAPI Injector::enableDebugPrivilege() noexcept
{
	HANDLE token{};
	if (OpenProcessToken(LI_FN(GetCurrentProcess).get()(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
		LUID value;
		if (LookupPrivilegeValueW(nullptr, _XorStrW(SE_DEBUG_NAME), &value)) {
			TOKEN_PRIVILEGES tp{};
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Luid = value;
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr))
				LI_FN(CloseHandle)(token);
		}
	}
}


void Injector::run() noexcept
{
	// 保留原有权限与加载能力；停止状态只枚举进程名，不打开游戏句柄。
	enableDebugPrivilege();
	while (true) {
		const auto& league_client_processes{ Injector::findProcesses(_XorStrW(L"LeagueClient.exe")) };
		const auto& league_processes{ Injector::findProcesses(_XorStrW(L"League of Legends.exe")) };

		R3nzSkinInjector::gameState = (league_processes.size() > 0) ? true : false;
		R3nzSkinInjector::clientState = (league_client_processes.size() > 0) ? true : false;
		
		// antiviruses don't like endless loops, show them that this loop is a breaking point. (technically still an infinite loop :D)
		if (league_processes.size() > 0xff)
			break;

		if (!R3nzSkinInjector::btnState.load() || league_processes.empty()) {
			R3nzSkinInjector::cheatState = false;
			std::this_thread::sleep_for(1s);
			continue;
		}
		for (auto& pid : league_processes) {
			R3nzSkinInjector::cheatState = R3nzSkinInjector::refresh_module_state(
				R3nzSkinInjector::btnState.load(),
				[pid] { return Injector::probeModule(pid); },
				[pid] {
					std::this_thread::sleep_for(1.5s);
					return R3nzSkinInjector::btnState.load() && Injector::inject(pid);
				});
			// 保留原来未确认加载时的轮询间隔，不因新增诊断加快重复尝试。
			if (!R3nzSkinInjector::cheatState.load())
				std::this_thread::sleep_for(1s);
		}
		std::this_thread::sleep_for(1s);
	}
}
