# 核心诊断构建

这是公开源码加载器与核心 DLL 的配套诊断构建，不是已修复的国服版本。不能用本 DLL 替换原版发行包 DLL：原版有 `NextHook` 导出，公开源码 DLL 没有该接口。本构建不添加新的加载方式。

## 构建与文件

在 VS 2022 工具环境执行 `./scripts/build-diagnostics.ps1 -Configuration ChinaServer`。
输出位于 `artifacts/diagnostics/ChinaServer`，包括 DLL、公开源码启动器、PDB 和含 SHA-256 的 `diagnostic-build.json`。CI 的独立 diagnostics 作业执行同样的构建和日志自检。普通发布构建默认没有这些日志。

诊断构建停用已有线程隐藏调用，保留调试符号。它不等价于原版闭源启动器，不应混用。没有运行游戏加载的自动测试。

## 查看实际执行阶段

启动器按原有方式使用；`InjectorDiagnostics.log` 记录其加载调用。核心若真正执行入口，会在该进程账户的临时目录写入 `R3nzSkin-core-<PID>.log`，通常是 `%TEMP%`。记录包含 PID、线程号、系统启动后毫秒数、阶段和数值错误码，不包含内存地址、账号或游戏内容。

| 最后阶段 | 含义 |
| --- | --- |
| process_attach | DLL 入口已在该 PID 执行；先核对 PID 是否为当前游戏 |
| worker_created / worker_create_failed | 启动线程创建结果；失败 code 属于 CRT errno |
| worker_enter | 核心工作线程进入 |
| client_search_begin / game_running_wait | 开始定位客户端 / 等待游戏进入运行状态 |
| full_search_begin / full_search_returned | 开始 / 返回完整定位流程，返回不等于每个字段已验证 |
| config_begin / config_loaded | 开始 / 返回配置加载 |
| hooks_install_begin / hooks_install_returned | 开始 / 返回原有绘制挂接流程，返回不等于已收到帧 |
| present_enter | 原有 Present 回调首次执行 |
| menu_draw_submitted | 首次提交菜单绘制，不等于用户已经看到菜单 |

阶段记录不改变错误判定；没有日志也不能单独证明未加载，还可能是目录不可写或入口前失败。需要结合 PID、启动器日志和系统事件。渲染阶段只记录一次，不逐帧写日志。

日志自检仅验证真实文件写入、PID 归属、错误码保留和并发完整性，不验证游戏加载或换肤。PDB 应与相同 SHA-256 的 DLL 配套使用。
