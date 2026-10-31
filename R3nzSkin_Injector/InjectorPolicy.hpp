#pragma once

namespace R3nzSkinInjector {
    enum class ModuleProbe { Absent, Present, Unavailable };

    // 将实际工作循环的查询与加载决策保留在可独立验证的边界内。
    template<class Probe, class Attempt>
    bool refresh_module_state(const bool enabled, Probe probe, Attempt attempt) {
        if (!enabled)
            return false;
        const auto before{ probe() };
        if (before == ModuleProbe::Present)
            return true;
        // 查询不可用不代表模块不存在，但也不能阻断用户已启用的原有加载流程。
        if (!attempt())
            return false;
        // 加载线程结束仅允许进行确认，不构成加载成功的证据。
        return probe() == ModuleProbe::Present;
    }

}
