#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace R3nzSkinInjector {
    enum class DiagnosticStage : std::uint8_t {
        Idle, OpenProbe, EnumModules, ModuleAbsent, ModulePresent,
        GetDirectory, OpenTarget, DllFile, AllocatePath, WritePath,
        CreateThread, WaitThread, ThreadExit, LoadCallReturned, ModuleName
    };
    enum class DiagnosticOutcome : std::uint8_t { Pending, Completed, Failed, NotObserved };
    enum class DiagnosticDomain : std::uint8_t { None, Win32, NtStatus, Crt, RawExit };

    struct Diagnostic {
        DiagnosticStage stage{ DiagnosticStage::Idle };
        DiagnosticOutcome outcome{ DiagnosticOutcome::Pending };
        DiagnosticDomain domain{ DiagnosticDomain::None };
        std::uint32_t code{ 0 };
    };

    constexpr std::uint64_t pack_diagnostic(const Diagnostic value) noexcept {
        return (static_cast<std::uint64_t>(value.stage) << 56)
            | (static_cast<std::uint64_t>(value.outcome) << 48)
            | (static_cast<std::uint64_t>(value.domain) << 40) | value.code;
    }
    constexpr Diagnostic unpack_diagnostic(const std::uint64_t value) noexcept {
        return { static_cast<DiagnosticStage>((value >> 56) & 0xFF),
            static_cast<DiagnosticOutcome>((value >> 48) & 0xFF),
            static_cast<DiagnosticDomain>((value >> 40) & 0xFF),
            static_cast<std::uint32_t>(value) };
    }

    // 每次读取获得同一阶段的完整结果，查询状态与加载状态互不覆盖。
    inline std::atomic<std::uint64_t> loadDiagnostic{ pack_diagnostic({}) };
    inline std::atomic<std::uint64_t> probeDiagnostic{ pack_diagnostic({}) };

    inline const char* diagnostic_stage_name(const DiagnosticStage stage) noexcept {
        switch (stage) {
        case DiagnosticStage::OpenProbe: return "OpenProcess(probe)";
        case DiagnosticStage::EnumModules: return "EnumProcessModules";
        case DiagnosticStage::ModuleName: return "GetModuleBaseName";
        case DiagnosticStage::ModuleAbsent: return "Module not observed";
        case DiagnosticStage::ModulePresent: return "Module observed";
        case DiagnosticStage::GetDirectory: return "GetCurrentDirectory";
        case DiagnosticStage::OpenTarget: return "OpenProcess(target)";
        case DiagnosticStage::DllFile: return "Read DLL file";
        case DiagnosticStage::AllocatePath: return "VirtualAllocEx";
        case DiagnosticStage::WritePath: return "WriteProcessMemory";
        case DiagnosticStage::CreateThread: return "NtCreateThreadEx";
        case DiagnosticStage::WaitThread: return "WaitForSingleObject";
        case DiagnosticStage::ThreadExit: return "Load thread exit value";
        case DiagnosticStage::LoadCallReturned: return "Load call returned";
        default: return "Not attempted";
        }
    }
    inline const char* diagnostic_outcome_name(const DiagnosticOutcome outcome) noexcept {
        switch (outcome) {
        case DiagnosticOutcome::Completed: return "completed";
        case DiagnosticOutcome::Failed: return "failed";
        case DiagnosticOutcome::NotObserved: return "not observed";
        default: return "pending";
        }
    }
    inline const char* diagnostic_domain_name(const DiagnosticDomain domain) noexcept {
        switch (domain) {
        case DiagnosticDomain::Win32: return "Win32";
        case DiagnosticDomain::NtStatus: return "NTSTATUS";
        case DiagnosticDomain::Crt: return "errno";
        case DiagnosticDomain::RawExit: return "raw exit";
        default: return "none";
        }
    }
    inline std::string format_diagnostic(const Diagnostic value) {
        char buffer[192]{};
        std::snprintf(buffer, sizeof(buffer), "%s: %s | %s %u (0x%08X)",
            diagnostic_stage_name(value.stage), diagnostic_outcome_name(value.outcome),
            diagnostic_domain_name(value.domain), static_cast<unsigned>(value.code),
            static_cast<unsigned>(value.code));
        return buffer;
    }
}
