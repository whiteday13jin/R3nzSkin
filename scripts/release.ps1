#requires -Version 5.1
param(
    [ValidateSet('All', 'ChinaServer', 'RiotGamesServers')]
    [string]$Configuration = 'All',
    [switch]$SkipBuild,
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
    & python -m unittest discover -s tests -v
    if ($LASTEXITCODE -ne 0) { throw '维护工具测试失败，停止发布' }
    if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration }
    $configurations = if ($Configuration -eq 'All') { @('ChinaServer', 'RiotGamesServers') } else { @($Configuration) }
    $packageArguments = @('tools/package_release.py', 'package', '--configurations') + $configurations
    & python @packageArguments
    if ($LASTEXITCODE -ne 0) { throw '打包或完整性检查失败，停止发布' }
    $releaseDir = Join-Path $repoRoot "artifacts\release\$($settings.version)"
    if ($CreateDraft) {
        # 显式创建草稿，不提供自动转为公开发行版的开关。
        $originUrl = & git remote get-url origin
        if ($originUrl -notmatch ('[/:]' + [regex]::Escape($settings.repository) + '(\.git)?$')) {
            throw 'origin 与 release.json 中的仓库不一致，停止创建草稿'
        }
        $commit = (& git rev-parse HEAD).Trim()
        $assets = @()
        foreach ($item in $configurations) {
            $stem = "R3nzSkin-$($settings.version)-$item-x64.zip"
            $assets += Join-Path $releaseDir $stem
            $assets += Join-Path $releaseDir "$stem.sha256"
        }
        $releaseArguments = @(
            'release', 'create', "v$($settings.version)", '--repo', $settings.repository,
            '--target', $commit, '--draft', '--prerelease',
            '--title', "R3nzSkin $($settings.version)（运行未验证）",
            '--notes-file', (Join-Path $releaseDir 'release-notes.md')
        ) + $assets
        & gh @releaseArguments
        if ($LASTEXITCODE -ne 0) { throw '创建 GitHub 草稿失败；本地发布包已保留' }
    }
    Write-Host "发布包及校验文件：$releaseDir"
} finally {
    Pop-Location
}
