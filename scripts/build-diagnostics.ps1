#requires -Version 5.1
param(
    [ValidateSet('ChinaServer', 'RiotGamesServers')][string]$Configuration = 'ChinaServer',
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
        $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if (-not $MSBuildPath) { throw '需要 VS 2022 MSBuild、v143 C++/CLI 与 Windows SDK；也可使用 Windows CI。' }

Push-Location $repoRoot
try {
    $destination = Join-Path $repoRoot "artifacts\diagnostics\$Configuration"
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $testOutput = Join-Path $repoRoot 'artifacts\core-diagnostics-test'
    & $MSBuildPath tests/native/CoreDiagnosticsTests.vcxproj /t:Rebuild /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$testOutput\" "/p:IntDir=$testOutput\obj\"
    if ($LASTEXITCODE -ne 0) { throw '核心诊断自检编译失败' }
    & (Join-Path $testOutput 'CoreDiagnosticsTests.exe')
    if ($LASTEXITCODE -ne 0) { throw '核心诊断自检失败' }
    $controlOutput = Join-Path $repoRoot 'artifacts\loader-control'
    foreach ($kind in @('Host', 'Dll', 'Controller')) {
        & $MSBuildPath tests/native/LoaderControlTests.vcxproj /t:Rebuild /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64 "/p:FixtureKind=$kind" "/p:OutDir=$controlOutput\" "/p:IntDir=$controlOutput\obj-$kind\"
        if ($LASTEXITCODE -ne 0) { throw "$kind 加载器对照编译失败" }
    }
    $control = Start-Process -FilePath (Join-Path $controlOutput 'LoaderControlTests.exe') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $controlOutput 'control.stdout.log') -RedirectStandardError (Join-Path $controlOutput 'control.stderr.log')
    if (-not $control.WaitForExit(30000)) {
        $control.Kill()
        $control.WaitForExit()
        throw '自有进程加载器对照超时；Job 将清理本次测试子进程'
    }
    Get-Content (Join-Path $controlOutput 'control.stdout.log')
    if ($control.ExitCode -ne 0) { throw "自有进程加载器对照失败：$($control.ExitCode)" }
    foreach ($project in @('R3nzSkin', 'R3nzSkin_Injector')) {
        $objectDir = Join-Path $repoRoot "artifacts\obj-diagnostics\$Configuration\$project"
        & $MSBuildPath "$project\$project.vcxproj" /m /t:Rebuild /nologo /verbosity:minimal /p:Platform=x64 "/p:Configuration=$Configuration" /p:R3nzDiagnostics=true "/p:SolutionDir=$repoRoot\" "/p:OutDir=$destination\" "/p:IntDir=$objectDir\"
        if ($LASTEXITCODE -ne 0) { throw "$project 诊断构建失败" }
    }
    $files = @{}
    foreach ($name in @('R3nzSkin.dll', 'R3nzSkin.pdb', 'R3nzSkin_Injector.exe')) {
        $path = Join-Path $destination $name
        if (-not (Test-Path -LiteralPath $path)) { throw "诊断构建缺少 $name" }
        $files[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    Copy-Item packaging/Launch.cmd, docs/CORE-DIAGNOSTICS.md -Destination $destination
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw '读取源码提交失败' }
    @{
        kind = 'core-diagnostics'; source_commit = $commit
        source_dirty = [bool](& git status --porcelain)
        configuration = $Configuration; runtime_verified = $false
        loader = 'public-source-only'; compatible_with_upstream_loader = $false
        files = $files
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'diagnostic-build.json') -Encoding UTF8
    Write-Host "诊断构建：$destination"
} finally { Pop-Location }
