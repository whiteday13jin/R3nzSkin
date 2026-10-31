"""回放日志验证诊断脚本；不访问目标进程。"""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which('pwsh'), '需要 PowerShell 7')
class DiagnosticReportTests(unittest.TestCase):
    def collect(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'manifest.json').write_text(json.dumps({
                'version': '16.21.1-fork.3', 'source_commit': 'a' * 40,
            }), encoding='utf-8')
            (root / 'InjectorDiagnostics.log').write_text('\n'.join(rows), encoding='utf-8')
            result = subprocess.run([
                shutil.which('pwsh'), '-NoProfile', '-File',
                str(Path(__file__).resolve().parents[1] / 'scripts/collect-diagnostics.ps1'),
                '-PackageDirectory', directory,
            ], capture_output=True)
            self.assertTrue((root / 'diagnostic-report.json').exists(), result.stderr)
            return result.returncode, json.loads((root / 'diagnostic-report.json').read_text(encoding='utf-8-sig'))

    @staticmethod
    def row(pid, channel, stage, outcome, domain='none', code=0):
        return (f'[DEBUG-r3diag] 2026-10-31T16:34:54Z pid={pid} channel={channel} '
                f'stage={stage} outcome={outcome} domain={domain} code={code} hex=0x{code:08X}')

    def test_actual_trace_keeps_initial_thread_result_and_query_failure(self):
        exit_code, report = self.collect([
            self.row(40140, 'load', 'Load thread exit value', 'completed', 'raw exit', 0xFFFFFFFF),
            self.row(40140, 'probe', 'EnumProcessModules', 'failed', 'Win32', 18),
            self.row(40140, 'load', 'VirtualAllocEx', 'failed', 'Win32', 0),
        ])
        self.assertEqual(exit_code, 1)
        self.assertEqual(report['verdict'], 'module_query_unavailable')
        self.assertEqual(report['first_load_result']['code'], 0xFFFFFFFF)
        self.assertEqual(report['first_recorded_failure']['stage'], 'EnumProcessModules')

    def test_latest_pid_does_not_inherit_an_old_process_success(self):
        exit_code, report = self.collect([
            self.row(1, 'probe', 'Module observed', 'completed'),
            self.row(2, 'load', 'Load thread exit value', 'completed', 'raw exit', 0xFFFFFFFF),
        ])
        self.assertEqual(exit_code, 2)
        self.assertEqual(report['target_pid'], 2)
        self.assertIsNone(report['last_probe'])
        self.assertEqual(report['verdict'], 'unverified')
        self.assertEqual(len(report['sessions']), 2)

    def test_only_observed_module_has_zero_exit_status(self):
        exit_code, report = self.collect([self.row(2, 'probe', 'Module observed', 'completed')])
        self.assertEqual(exit_code, 0)
        self.assertEqual(report['verdict'], 'module_observed_only')
