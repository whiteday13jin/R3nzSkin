#include <cstdio>

void click_start();
void click_stop();
bool worker_should_inject();
void report_loaded();
bool ui_shows_injected();
bool ui_sees_game();
bool ui_sees_client();

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
    if (!failures) {
        std::puts("PASS: Start, Stop and status cross translation units");
    }
    return failures ? 1 : 0;
}
