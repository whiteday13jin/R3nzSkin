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
    // 查询不可用时保留用户启用的加载流程，但不能误报模块已确认存在。
    const bool unavailable{ refresh_module_state(true, probe, attempt) };
    check(!unavailable && attempts == 1 && probes == 2,
        "FAIL: unavailable query blocked the original load path or claimed confirmed success");

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
    if (!failures) std::puts("PASS: enabled load path preserved; stopped worker does not probe; thread completion requires module confirmation");
    return failures;
}
