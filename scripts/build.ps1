#requires -Version 5.1
param(
    [ValidateSet('All', 'ChinaServer', 'RiotGamesServers')]
    [string]$Configuration = 'All',
    [string]$MSBuildPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $MSBuildPath) {
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($command) { $MSBuildPath = $command.Source }
}
if (-not $MSBuildPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
            Select-Object -First 1
    }
}
if (-not $MSBuildPath -or -not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
    throw '未找到 MSBuild。请安装 VS 2022 C++ 桌面开发、v143 C++/CLI、Windows SDK 和 .NET Framework 4.8 开发工具，或使用 GitHub Actions。'
}

Push-Location $repoRoot
try {
    $sourceCommit = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw '无法读取源码提交号' }
    $sourceDirty = [bool](& git status --porcelain)
    # 用不同翻译单元覆盖实际的界面/后台状态共享，不访问游戏进程。
    $testOutputDir = Join-Path $repoRoot 'artifacts\native-test'
    $testObjectDir = Join-Path $repoRoot 'artifacts\obj\state-tests'
    $testArguments = @(
        (Join-Path $repoRoot 'tests\native\InjectorStateTests.vcxproj'),
        '/t:Rebuild', '/nologo', '/verbosity:minimal', '/p:Platform=x64', '/p:Configuration=Release',
        "/p:OutDir=$testOutputDir\", "/p:IntDir=$testObjectDir\"
    )
    & $MSBuildPath @testArguments
    if ($LASTEXITCODE -ne 0) { throw '启动器跨文件状态测试编译失败' }
    & (Join-Path $testOutputDir 'InjectorStateTests.exe')
    if ($LASTEXITCODE -ne 0) { throw '界面与后台状态未同步，停止构建发布' }
    $configurations = if ($Configuration -eq 'All') { @('ChinaServer', 'RiotGamesServers') } else { @($Configuration) }
    foreach ($item in $configurations) {
        $buildDir = Join-Path $repoRoot "artifacts\build\$item"
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
        foreach ($project in @('R3nzSkin', 'R3nzSkin_Injector')) {
            $projectPath = Join-Path $repoRoot "$project\$project.vcxproj"
            $objectDir = Join-Path $repoRoot "artifacts\obj\$item\$project"
            $buildArguments = @(
                $projectPath, '/m', '/t:Rebuild', '/nologo', '/verbosity:minimal',
                '/p:Platform=x64', "/p:Configuration=$item",
                "/p:SolutionDir=$repoRoot\", "/p:OutDir=$buildDir\", "/p:IntDir=$objectDir\"
            )
            & $MSBuildPath @buildArguments
            if ($LASTEXITCODE -ne 0) { throw "$project / $item 编译失败（退出码 $LASTEXITCODE）" }
        }
        $binaries = @{}
        foreach ($name in @('R3nzSkin.dll', 'R3nzSkin_Injector.exe')) {
            $binary = Join-Path $buildDir $name
            if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "构建缺少 $name" }
            $binaries[$name] = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        # 构建记录绑定源码提交和两个二进制，阻止后续把旧产物混入新发布包。
        @{
            source_commit = $sourceCommit.Trim()
            source_dirty = $sourceDirty
            configuration = $item
            platform = 'x64'
            binaries = $binaries
        } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $buildDir 'build-info.json') -Encoding UTF8
        Write-Host "构建完成：$buildDir"
    }
} finally {
    Pop-Location
}
