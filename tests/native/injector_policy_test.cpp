#include <cstdio>
#include <initializer_list>
#include "../../R3nzSkin_Injector/InjectorPolicy.hpp"

int test_injector_policy() {
    using namespace R3nzSkinInjector;
    int failures{};
    const auto check = [&failures](bool condition, const char* message) {
        if (!condition) { std::puts(message); ++failures; }
    };
    int attempts{}, probes{};
    auto probe = [&]() { ++probes; return ModuleProbe::Unavailable; };
    auto attempt = [&]() { ++attempts; return true; };
    // 回放实际 EnumProcessModules 失败：查询不可用时不得再进入加载分支。
    const bool unavailable{ refresh_module_state(true, probe, attempt) };
    check(!unavailable && attempts == 0,
        "FAIL: unavailable module query triggered a load or claimed success");

    for (const auto after : { ModuleProbe::Absent, ModuleProbe::Unavailable, ModuleProbe::Present }) {
        attempts = probes = 0;
        auto sequence = [&]() { return ++probes == 1 ? ModuleProbe::Absent : after; };
        // 模拟实际线程正常结束（原始退出值可以是 0xFFFFFFFF）；不等于模块已加载。
        const bool confirmed{ refresh_module_state(true, sequence, attempt) };
        check(confirmed == (after == ModuleProbe::Present) && attempts == 1 && probes == 2,
            "FAIL: completed thread was not followed by a successful module confirmation");
    }
    attempts = 0;
    check(refresh_module_state(true, [] { return ModuleProbe::Present; }, attempt) && attempts == 0,
        "FAIL: present module triggered another load");
    check(!refresh_module_state(false, [] { return ModuleProbe::Absent; }, attempt) && attempts == 0,
        "FAIL: stopped worker attempted a load");
    check(!refresh_module_state(true, [] { return ModuleProbe::Absent; }, [] { return false; }),
        "FAIL: failed load was reported as confirmed");
    probes = attempts = 0;
    check(!refresh_module_state(false, probe, attempt) && probes == 0 && attempts == 0,
        "FAIL: stopped worker opened target for module query");
    ModuleSession session;
    check(!session.refresh(true, probe, attempt), "FAIL: unavailable query claimed success");
    check(!session.refresh(true, probe, attempt) && probes == 1 && attempts == 0,
        "FAIL: paused session repeatedly queried target");
    session.refresh(false, probe, attempt);
    session.refresh(true, probe, attempt);
    check(probes == 2 && attempts == 0, "FAIL: explicit stop did not reset query pause");
    if (!failures) std::puts("PASS: unavailable queries suppress loading; completed threads require module confirmation");
    return failures;
}
