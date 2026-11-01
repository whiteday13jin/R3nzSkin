#pragma warning(disable : 6387 4715)

#include <Windows.h>
#include <clocale>
#include <cerrno>
#include <chrono>
#include <thread>

#include "CheatManager.hpp"

#include "Config.hpp"
#include "Hooks.hpp"
#include "Memory.hpp"

#include "SDK/GameState.hpp"
#include "CoreDiagnostics.hpp"

bool WINAPI HideThread(const HANDLE hThread) noexcept
{
	__try {
		using FnSetInformationThread = NTSTATUS(NTAPI*)(HANDLE ThreadHandle, UINT ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength);
		const auto NtSetInformationThread{ reinterpret_cast<FnSetInformationThread>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtSetInformationThread")) };

		if (!NtSetInformationThread)
			return false;

		if (const auto status{ NtSetInformationThread(hThread, 0x11u, nullptr, 0ul) }; status == 0x00000000)
			return true;
	} __except (TRUE) {
		return false;
	}
	return false;
}

__declspec(safebuffers) static void WINAPI DllAttach([[maybe_unused]] LPVOID lp) noexcept
{
	using namespace std::chrono_literals;

	CoreDiagnostics::record("worker_enter");
	cheatManager.start();
#ifndef R3NZ_DIAGNOSTICS
	if (HideThread(::GetCurrentThread()))
		cheatManager.logger->addLog("Thread Hidden!\n");
#endif

	CoreDiagnostics::record("client_search_begin");
	cheatManager.memory->Search(true);
	CoreDiagnostics::record("game_running_wait");
	while (true) {
		std::this_thread::sleep_for(1s);
		
		if (!cheatManager.memory->client)
			cheatManager.memory->Search(true);
		else if (cheatManager.memory->client->get_game_state() == GGameState_s::Running)
			break;
	}
	cheatManager.logger->addLog("GameClient found!\n");
	CoreDiagnostics::record("game_running");
	
	std::this_thread::sleep_for(500ms);
	CoreDiagnostics::record("full_search_begin");
	cheatManager.memory->Search(false);
	CoreDiagnostics::record("full_search_returned");
	cheatManager.logger->addLog("All offsets found!\n");
	std::this_thread::sleep_for(500ms);

	CoreDiagnostics::record("config_begin");
	cheatManager.config->init();
	cheatManager.config->load();
	cheatManager.gui->is_open = cheatManager.config->isOpen;
	cheatManager.logger->addLog("CFG loaded!\n");
	CoreDiagnostics::record("config_loaded");
	
	CoreDiagnostics::record("hooks_install_begin");
	cheatManager.hooks->install();
	CoreDiagnostics::record("hooks_install_returned");
		
	while (cheatManager.cheatState)
		std::this_thread::sleep_for(250ms);

	::ExitProcess(0u);
}

__declspec(safebuffers) BOOL APIENTRY DllMain(const HMODULE hModule, const DWORD reason, [[maybe_unused]] LPVOID reserved)
{
	DisableThreadLibraryCalls(hModule);

	if (reason != DLL_PROCESS_ATTACH)
		return FALSE;

#ifdef R3NZ_DIAGNOSTICS
	CoreDiagnostics::record("process_attach");
#else
	HideThread(hModule);
#endif
	std::setlocale(LC_ALL, ".utf8");

	const auto worker = ::_beginthreadex(nullptr, 0u, reinterpret_cast<_beginthreadex_proc_type>(DllAttach), nullptr, 0u, nullptr);
#ifdef R3NZ_DIAGNOSTICS
	CoreDiagnostics::record(worker ? "worker_created" : "worker_create_failed", worker ? 0 : static_cast<DWORD>(errno));
	if (worker) ::CloseHandle(reinterpret_cast<HANDLE>(worker));
#else
	::CloseHandle(hModule);
#endif
	return TRUE;
}
