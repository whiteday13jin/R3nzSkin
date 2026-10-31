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
$targetPid = $entries[-1].pid
$currentEntries = @($entries | Where-Object pid -eq $targetPid)
$lastLoad = $currentEntries | Where-Object channel -eq 'load' | Select-Object -Last 1
$lastProbe = $currentEntries | Where-Object channel -eq 'probe' | Select-Object -Last 1
$firstLoad = $currentEntries | Where-Object { $_.channel -eq 'load' -and $_.outcome -ne 'pending' } | Select-Object -First 1
$firstFailure = $currentEntries | Where-Object outcome -eq 'failed' | Select-Object -First 1
$sessions = @($entries | Group-Object pid | ForEach-Object {
    [ordered]@{
        pid = [uint32]$_.Name
        first_time_utc = $_.Group[0].time_utc
        last_time_utc = $_.Group[-1].time_utc
        first_load_result = ($_.Group | Where-Object { $_.channel -eq 'load' -and $_.outcome -ne 'pending' } | Select-Object -First 1)
        first_recorded_failure = ($_.Group | Where-Object outcome -eq 'failed' | Select-Object -First 1)
    }
})
$verdict = 'unverified'
if ($lastProbe -and $lastProbe.outcome -eq 'failed') { $verdict = 'module_query_unavailable' }
elseif ($lastLoad -and $lastLoad.outcome -eq 'failed') { $verdict = 'load_stage_failed' }
elseif ($lastProbe -and $lastProbe.stage -eq 'Module observed') { $verdict = 'module_observed_only' }
$report = [ordered]@{
    package_version = $manifest.version
    source_commit = $manifest.source_commit
    game_version = $gameVersion
    verdict = $verdict
    target_pid = $targetPid
    first_load_result = $firstLoad
    first_recorded_failure = $firstFailure
    last_load = $lastLoad
    last_probe = $lastProbe
    sessions = $sessions
    entries = $entries
}
$reportPath = Join-Path $packagePath 'diagnostic-report.json'
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output ('诊断报告：' + $reportPath)
if ($lastLoad) { Write-Output ('Load: ' + $lastLoad.stage + ' / ' + $lastLoad.outcome + ' / ' + $lastLoad.domain + ' ' + $lastLoad.hex) }
if ($lastProbe) { Write-Output ('Probe: ' + $lastProbe.stage + ' / ' + $lastProbe.outcome + ' / ' + $lastProbe.domain + ' ' + $lastProbe.hex) }
if ($verdict -in @('load_stage_failed', 'module_query_unavailable')) { exit 1 }
if ($verdict -eq 'module_observed_only') { exit 0 }
exit 2
