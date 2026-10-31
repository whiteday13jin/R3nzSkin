#include "../../R3nzSkin_Injector/InjectorState.hpp"

// 与界面分开编译，覆盖 Injector.cpp 与 main.cpp 各自包含头文件的调用关系。
bool worker_should_inject() { return R3nzSkinInjector::btnState; }
void report_loaded() {
    R3nzSkinInjector::gameState = true;
    R3nzSkinInjector::clientState = true;
    R3nzSkinInjector::cheatState = true;
}

void report_fixture_error() {
    using namespace R3nzSkinInjector;
    loadDiagnostic = pack_diagnostic({ DiagnosticStage::CreateThread, DiagnosticOutcome::Failed,
        DiagnosticDomain::NtStatus, 0xC0000022u });
    probeDiagnostic = pack_diagnostic({ DiagnosticStage::OpenProbe, DiagnosticOutcome::Failed,
        DiagnosticDomain::Win32, 5u });
}
