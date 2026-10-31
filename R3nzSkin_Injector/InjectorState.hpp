#pragma once

#include <atomic>

namespace R3nzSkinInjector {

    // C++17 内联变量在全部翻译单元间共享；原子读写同步界面与后台线程。
    inline std::atomic_bool btnState{ false };
    inline std::atomic_bool gameState{ false };
    inline std::atomic_bool cheatState{ false };
    inline std::atomic_bool clientState{ false };
}
