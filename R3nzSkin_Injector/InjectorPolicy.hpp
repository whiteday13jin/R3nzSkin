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
        // 查询失败不能视为模块不存在，也不能据此触发加载或重试。
        if (before != ModuleProbe::Absent || !attempt())
            return false;
        // 加载线程结束仅允许进行确认，不构成加载成功的证据。
        return probe() == ModuleProbe::Present;
    }

    class ModuleSession {
        bool queryUnavailable{ false };
    public:
        template<class Probe, class Attempt>
        bool refresh(const bool enabled, Probe probe, Attempt attempt) {
            if (!enabled) { queryUnavailable = false; return false; }
            // 同一会话查询失败后保持暂停，避免每秒重新打开目标句柄。
            if (queryUnavailable)
                return false;
            return refresh_module_state(true, [&] {
                const auto result{ probe() };
                queryUnavailable = result == ModuleProbe::Unavailable;
                return result;
            }, attempt);
        }
    };
}
