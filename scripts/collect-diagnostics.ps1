#requires -Version 5.1
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [switch]$Interactive
)

# 按诊断 Skill 的人工复现模板收集结果，不启动或修改游戏进程。
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$packagePath = (Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $packagePath 'manifest.json') -Raw | ConvertFrom-Json
$gameVersion = ''
if ($Interactive) {
    Write-Host ('打开此目录中的 Launch.cmd：' + $packagePath)
    Write-Host '在指定测试环境进入训练后点击 Start；出现结果后点 Stop。此脚本不会代为启动。'
    [void](Read-Host '完成这一轮后按 Enter')
    $gameVersion = Read-Host '游戏完整版本（不知道可留空）'
}
$logPath = Join-Path $packagePath 'InjectorDiagnostics.log'
if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
    Write-Output '尚无诊断日志：先完成一次人工测试。'
    exit 2
}
$entries = @()
foreach ($line in (Get-Content -LiteralPath $logPath -Tail 200)) {
    $taskMatch = [regex]::Match($line,
        '\[DEBUG-r3diag\] (\S+) pid=(\d+) channel=(load|probe) stage=(.*?) outcome=(.*?) domain=(.*?) code=(\d+) hex=(0x[0-9A-Fa-f]+)')
    if ($taskMatch.Success) {
        $entries += [pscustomobject]@{
            time_utc = $taskMatch.Groups[1].Value
            pid = [uint32]$taskMatch.Groups[2].Value
            channel = $taskMatch.Groups[3].Value
            stage = $taskMatch.Groups[4].Value
            outcome = $taskMatch.Groups[5].Value
            domain = $taskMatch.Groups[6].Value
            code = [uint32]$taskMatch.Groups[7].Value
            hex = $taskMatch.Groups[8].Value
        }
    }
}
if ($entries.Count -eq 0) { Write-Output '日志中没有可解析的诊断结果'; exit 2 }
$lastLoad = $entries | Where-Object channel -eq 'load' | Select-Object -Last 1
$lastProbe = $entries | Where-Object channel -eq 'probe' | Select-Object -Last 1
$verdict = 'unverified'
if ($lastLoad -and $lastLoad.outcome -eq 'failed') { $verdict = 'load_stage_failed' }
elseif ($lastProbe -and $lastProbe.stage -eq 'Module observed') { $verdict = 'module_observed_only' }
$report = [ordered]@{
    package_version = $manifest.version
    source_commit = $manifest.source_commit
    game_version = $gameVersion
    verdict = $verdict
    last_load = $lastLoad
    last_probe = $lastProbe
    entries = $entries
}
$reportPath = Join-Path $packagePath 'diagnostic-report.json'
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output ('诊断报告：' + $reportPath)
if ($lastLoad) { Write-Output ('Load: ' + $lastLoad.stage + ' / ' + $lastLoad.outcome + ' / ' + $lastLoad.domain + ' ' + $lastLoad.hex) }
if ($lastProbe) { Write-Output ('Probe: ' + $lastProbe.stage + ' / ' + $lastProbe.outcome + ' / ' + $lastProbe.domain + ' ' + $lastProbe.hex) }
if ($verdict -eq 'load_stage_failed') { exit 1 }
exit 0
