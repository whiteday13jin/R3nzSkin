#requires -Version 5.1
param(
    [ValidateSet('All', 'ChinaServer', 'RiotGamesServers')]
    [string]$Configuration = 'All',
    [switch]$SkipBuild,
    [string]$PackageDirectory,
    [switch]$CreateDraft
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    $dirty = & git status --porcelain
    if ($LASTEXITCODE -ne 0 -or $dirty) { throw '发布要求干净的 Git 工作区，请先提交源码改动。' }
    $settings = Get-Content -LiteralPath 'release.json' -Raw | ConvertFrom-Json
    $commit = (& git rev-parse HEAD).Trim()
    & python -m unittest discover -s tests -v
    if ($LASTEXITCODE -ne 0) { throw '维护工具测试失败，停止发布' }
    $configurations = if ($Configuration -eq 'All') { @('ChinaServer', 'RiotGamesServers') } else { @($Configuration) }
    if ($PackageDirectory) {
        if ($SkipBuild) { throw '-PackageDirectory 与 -SkipBuild 不能同时使用' }
        $releaseDir = (Resolve-Path -LiteralPath $PackageDirectory).Path
    } else {
        if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration }
        $packageArguments = @('tools/package_release.py', 'package', '--configurations') + $configurations
        & python @packageArguments
        if ($LASTEXITCODE -ne 0) { throw '打包或完整性检查失败，停止发布' }
        $releaseDir = Join-Path $repoRoot "artifacts\release\$($settings.version)"
    }
    $assets = @()
    foreach ($item in $configurations) {
        $stem = "R3nzSkin-$($settings.version)-$item-x64.zip"
        $matches = @(Get-ChildItem -LiteralPath $releaseDir -Filter $stem -File -Recurse)
        if ($matches.Count -ne 1) { throw "$stem 必须恰好存在一份，实际找到 $($matches.Count) 份" }
        $zipPath = $matches[0].FullName
        & python tools/package_release.py verify $zipPath --source-commit $commit --version $settings.version --configuration $item --require-checksum
        if ($LASTEXITCODE -ne 0) { throw "$item 发布包来源或完整性校验失败" }
        $assets += $zipPath
        $assets += "$zipPath.sha256"
    }
    if ($CreateDraft) {
        # 显式创建草稿，不提供自动转为公开发行版的开关。
        $originUrl = & git remote get-url origin
        if ($originUrl -notmatch ('[/:]' + [regex]::Escape($settings.repository) + '(\.git)?$')) {
            throw 'origin 与 release.json 中的仓库不一致，停止创建草稿'
        }
        $notesDir = Join-Path $repoRoot 'artifacts\draft-notes'
        New-Item -ItemType Directory -Path $notesDir -Force | Out-Null
        $notesPath = Join-Path $notesDir "$($settings.version).md"
        @"
# R3nzSkin $($settings.version)

源码提交：$commit；上游基线：$($settings.upstream_commit)。
目标游戏补丁：$($settings.game_patch)；Windows x64。

包内包含公开源码构建的 DLL 和历史启动器、中文说明及 SHA-256 校验文件。
启动器不再从旧仓库自动更新，也不随机重命名自身。

**游戏内运行能力尚未验证，不包含上游未开源加载器，不代表恢复国服支持。**
"@ | Set-Content -LiteralPath $notesPath -Encoding UTF8
        $releaseArguments = @(
            'release', 'create', "v$($settings.version)", '--repo', $settings.repository,
            '--target', $commit, '--draft', '--prerelease',
            '--title', "R3nzSkin $($settings.version)（运行未验证）",
            '--notes-file', $notesPath
        ) + $assets
        & gh @releaseArguments
        if ($LASTEXITCODE -ne 0) { throw '创建 GitHub 草稿失败；本地发布包已保留' }
    }
    Write-Host "发布包及校验文件：$releaseDir"
} finally {
    Pop-Location
}
