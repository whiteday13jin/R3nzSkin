# 构建与发布

本轮版本：`16.21.1-fork.3`（诊断版）；核心上游：`hydy100/R3nzSkin@8ea86a63a261caf0619af6c12777acf19b300d2b`。

## 一次完成构建与打包

要求 Windows、Git、Python 3.10+、VS 2022 的 C++ 桌面开发、v143 C++/CLI、Windows SDK 和 .NET Framework 4.8 开发工具。
在仓库根目录执行：

```powershell
.\scripts\release.ps1
```

流程会检查 Git 工作区、运行维护工具测试、编译两套 x64 的 DLL 与公开启动器、校验构建记录并生成 ZIP。
编译前还会用 MSVC 构建、执行跨翻译单元的启动器状态测试，阻止 Start/Stop 与界面状态再次相互隔离。
只构建国服工程配置时使用 `-Configuration ChinaServer`。
默认不会运行生成的程序，也不会创建或公开 GitHub Release。

构建缺少工具链时会明确退出，不自动安装系统组件。
只编译不打包可执行 `.\scripts\build.ps1 -Configuration ChinaServer`；允许在未提交的工作区编译，但此类产物不能发布。

输出目录为 `artifacts/release/<版本>/`，包含各配置的 ZIP、同名 SHA-256 文件及 `release-notes.md`。
每个 ZIP 包含二进制、中文使用说明、稳定启动入口、许可证和逐文件校验清单。
同版本文件已存在时拒绝覆盖。需要重打时使用新的输出目录，或完成验证后递增 `release.json` 版本。

## 没有本地 C++ 工具链

将已提交代码推送到本 Fork，在 Actions 中运行 `Windows` 工作流。
该工作流对两个 x64 配置分别构建和打包，并上传 `release-<配置>` Artifact。
下载 Artifact 后解压外层容器，里面才是正式校验的 ZIP 和 `.sha256` 文件。
Actions 通过只证明构建、维护测试与包完整性通过，不证明游戏内运行能力。

若要从 CI 产物直接准备发布，无需本地 C++ 工具链。先切换到 CI 对应的准确源码提交，并下载该次运行的产物：

```powershell
gh run download <运行ID> --repo whiteday13jin/R3nzSkin --dir artifacts/downloaded
.\scripts\release.ps1 -PackageDirectory artifacts/downloaded
```

脚本会递归查找两个配置的包，要求各自恰好一份，并检查源码提交、版本、配置和内外 SHA-256。
不需要重新打包；确认后给同一命令增加 `-CreateDraft` 即可创建草稿。

## 从已有构建产物打包

同一源码提交下已有 `artifacts/build/<配置>/build-info.json` 及两个二进制时：

```powershell
.\scripts\release.ps1 -SkipBuild -Configuration ChinaServer
```

旧提交、未提交源码或构建后二进制发生变化时会拒绝打包。
构建记录并非签名证明；应只从可信的 CI 或自己构建的输出打包。

## 创建待审阅的 GitHub Release

先登录 `gh`，确认 `origin` 为 `release.json` 中的本 Fork，递增版本并提交代码，然后执行：

```powershell
.\scripts\release.ps1 -CreateDraft
```

或在已有可信构建、尚未生成同名发布包时加 `-SkipBuild`。
已有 CI ZIP 则使用 `-PackageDirectory`，不要与 `-SkipBuild` 混用。
脚本始终创建 **draft + prerelease**，上传本轮生成的 ZIP 和 SHA-256；不会覆盖现有 Release，也不会自动公开发布。
发布说明自动写入精确源码提交、上游基线与未验证的运行状态。
公开发布前核对二进制来源、校验结果与运行验证记录，不将工程配置名称当成服务器兼容证明。

## 下一次维护

1. `git fetch upstream`，审查并合并上游实际补丁。
2. 更新 `release.json` 中的版本、游戏补丁和完整上游提交号，并同步使用说明。
3. 对目标游戏磁盘文件重新执行 `tools/inspect_compatibility.py`；保持运行验证与静态检查分开记录。
4. 提交后重新执行发布流程。保留最小差异，优先跟随上游核心适配。
