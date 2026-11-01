#include "../../R3nzSkin/CoreDiagnostics.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

int main()
{
    wchar_t path[MAX_PATH]{};
    if (!CoreDiagnostics::log_path(path)) return 1;
    // 测试只写本测试进程的临时日志，不加载核心或访问游戏。
    ::DeleteFileW(path);
    ::SetLastError(1234);
    CoreDiagnostics::record("test_control", 5678);
    if (::GetLastError() != 1234) return 2;
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([] {
        for (int n = 0; n < 10; ++n) CoreDiagnostics::record("test_concurrent");
    });
    for (auto& worker : workers) worker.join();
    std::ifstream file(path);
    std::string line;
    unsigned lines = 0;
    const auto pid = "pid=" + std::to_string(::GetCurrentProcessId()) + " ";
    while (std::getline(file, line)) {
        if (line.find("[R3NZ-CORE] " + pid) != 0 || line.find(" stage=test_") == std::string::npos) return 3;
        if (lines == 0 && line.find("stage=test_control code=5678") == std::string::npos) return 4;
        ++lines;
    }
    file.close();
    ::DeleteFileW(path);
    if (lines != 81) return 5;
    std::cout << "PASS: PID attribution, error preservation and concurrent diagnostic records\n";
}
