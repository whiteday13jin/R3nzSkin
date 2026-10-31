#include "../../R3nzSkin_Injector/InjectorState.hpp"

// 独立翻译单元模拟界面使用的真实共享状态，保留实际读写方式。
void click_start() { R3nzSkinInjector::btnState = true; }
void click_stop() { R3nzSkinInjector::btnState = false; }
bool ui_shows_injected() { return R3nzSkinInjector::cheatState; }
bool ui_sees_game() { return R3nzSkinInjector::gameState; }
bool ui_sees_client() { return R3nzSkinInjector::clientState; }
