#include <cstdio>
#include <string>
#include "../../R3nzSkin_Injector/InjectorDiagnostics.hpp"

void click_start();
void click_stop();
bool worker_should_inject();
void report_loaded();
bool ui_shows_injected();
bool ui_sees_game();
bool ui_sees_client();
void report_fixture_error();
std::string ui_load_diagnostic();
std::string ui_probe_diagnostic();

int main() {
    int failures = 0;
    click_start();
    if (!worker_should_inject()) {
        std::puts("FAIL: Start clicked, worker still sees stopped");
        ++failures;
    }
    report_loaded();
    if (!ui_shows_injected()) {
        std::puts("FAIL: worker reports loaded, UI still shows Not Injected");
        ++failures;
    }
    if (!ui_sees_game() || !ui_sees_client()) {
        std::puts("FAIL: process status is not shared with UI");
        ++failures;
    }
    click_stop();
    if (worker_should_inject()) {
        std::puts("FAIL: Stop does not reach worker");
        ++failures;
    }
    report_fixture_error();
    const auto loadMessage{ ui_load_diagnostic() };
    const auto probeMessage{ ui_probe_diagnostic() };
    if (loadMessage.find("NtCreateThreadEx: failed") == std::string::npos
        || loadMessage.find("NTSTATUS") == std::string::npos
        || loadMessage.find("0xC0000022") == std::string::npos
        || probeMessage.find("OpenProcess(probe): failed") == std::string::npos
        || probeMessage.find("Win32 5") == std::string::npos) {
        std::puts("FAIL: diagnostic stage/domain/code lost across translation units");
        ++failures;
    }
    using namespace R3nzSkinInjector;
    const auto highBits{ unpack_diagnostic(pack_diagnostic({ DiagnosticStage::ThreadExit,
        DiagnosticOutcome::Completed, DiagnosticDomain::RawExit, 0xFFFFFFFFu })) };
    if (highBits.code != 0xFFFFFFFFu || highBits.domain != DiagnosticDomain::RawExit
        || highBits.outcome != DiagnosticOutcome::Completed) {
        std::puts("FAIL: diagnostic packing truncates raw values");
        ++failures;
    }
    if (!failures) {
        std::puts("PASS: Start, Stop and status cross translation units");
        std::puts("PASS: separate load/probe diagnostic stages and complete error codes");
    }
    return failures ? 1 : 0;
}
