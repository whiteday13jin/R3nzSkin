#requires -Version 5.1
param(
    [Parameter(Mandatory = $true)][uint32[]]$TargetProcessIds,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

# 仅查询状态、架构、权限与模块数量；不输出模块地址，不修改目标进程。
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public sealed class ProcessAccessResult {
    public uint Pid;
    public int QueryError;
    public uint? ExitCode;
    public ushort? ProcessMachine;
    public ushort? NativeMachine;
    public bool? Elevated;
    public int ModulesOpenError;
    public uint ModulesRequestedAccess = 0x410;
    public uint? ModulesGrantedAccess;
    public int? HandleQueryStatus;
    public bool? ModulesSucceeded;
    public int? ModulesError;
    public uint? ModuleCount;
}
public static class ReadOnlyProcessAccess {
    [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h, int c, [Out] uint[] info, uint size, out uint returned);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint a, bool b, uint p);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr h, out uint c);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool IsWow64Process2(IntPtr h, out ushort p, out ushort n);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool K32EnumProcessModules(IntPtr h, [Out] IntPtr[] m, uint s, out uint n);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr h, uint a, out IntPtr t);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr h, int c, out int v, uint n, out uint r);
    public static ProcessAccessResult Inspect(uint pid) {
        var result = new ProcessAccessResult { Pid = pid };
        var h = OpenProcess(0x1000, false, pid);
        int error = Marshal.GetLastWin32Error();
        if (h == IntPtr.Zero) { result.QueryError = error; return result; }
        try {
            uint code;
            if (GetExitCodeProcess(h, out code)) result.ExitCode = code;
            ushort process, native;
            if (IsWow64Process2(h, out process, out native)) {
                result.ProcessMachine = process;
                result.NativeMachine = native;
            }
            IntPtr token;
            if (OpenProcessToken(h, 8, out token)) {
                try {
                    int value; uint size;
                    if (GetTokenInformation(token, 20, out value, 4, out size)) result.Elevated = value != 0;
                } finally { CloseHandle(token); }
            }
        } finally { CloseHandle(h); }
        // 只申请查询和读取权限；不提权、不重试其他访问方式。
        var modulesHandle = OpenProcess(0x410, false, pid);
        error = Marshal.GetLastWin32Error();
        if (modulesHandle == IntPtr.Zero) { result.ModulesOpenError = error; return result; }
        try {
            var basic = new uint[14]; uint returned;
            int status = NtQueryObject(modulesHandle, 0, basic, 56, out returned);
            result.HandleQueryStatus = status;
            if (status >= 0) result.ModulesGrantedAccess = basic[1];
            var modules = new IntPtr[2048]; uint needed;
            bool success = K32EnumProcessModules(modulesHandle, modules, (uint)(modules.Length * IntPtr.Size), out needed);
            error = Marshal.GetLastWin32Error();
            result.ModulesSucceeded = success;
            result.ModulesError = success ? 0 : error;
            if (success) result.ModuleCount = needed / (uint)IntPtr.Size;
        } finally { CloseHandle(modulesHandle); }
        return result;
    }
}
'@

$report = [ordered]@{
    time_utc = [DateTime]::UtcNow.ToString('o')
    observer = [ReadOnlyProcessAccess]::Inspect([uint32]$PID)
    targets = @($TargetProcessIds | ForEach-Object { [ReadOnlyProcessAccess]::Inspect($_) })
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$report | ConvertTo-Json -Depth 5
