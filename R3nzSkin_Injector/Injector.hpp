#pragma once

#include <Windows.h>
#include <chrono>
#include <cinttypes>
#include <string>
#include <vector>
#include "InjectorPolicy.hpp"

using namespace std::chrono_literals;
using proclist_t = std::vector<std::uint32_t>;

NTSTATUS NTAPI NtCreateThreadEx(PHANDLE, ACCESS_MASK, LPVOID, HANDLE, LPTHREAD_START_ROUTINE, LPVOID, BOOL, SIZE_T, SIZE_T, SIZE_T, LPVOID);

class Injector {
public:
	static proclist_t WINAPI findProcesses(const std::wstring& name) noexcept;
	static R3nzSkinInjector::ModuleProbe WINAPI probeModule(const std::uint32_t pid) noexcept;
	static bool WINAPI inject(const std::uint32_t pid) noexcept;
	static void WINAPI enableDebugPrivilege() noexcept;
	static void run() noexcept;
};
