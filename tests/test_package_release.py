"""验证发布包来源绑定、混包拒绝和完整性；不运行任何游戏程序。"""

import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from test_inspect_compatibility import make_pe
from tools.package_release import digest, package_configuration, verify_archive


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'packaging').mkdir()
        (self.root / 'docs').mkdir()
        (self.root / 'packaging' / 'Launch.cmd').write_text('@echo off\n', encoding='utf-8')
        (self.root / 'docs' / 'USAGE.md').write_text('运行未验证\n', encoding='utf-8')
        (self.root / 'LICENSE').write_text('fixture license\n', encoding='utf-8')
        self.commit = 'a' * 40
        self.settings = {
            'version': '16.21.1-fork.1', 'game_patch': '16.21',
            'repository': 'example/fork', 'upstream_repository': 'example/upstream',
            'upstream_commit': 'b' * 40,
            'runtime_verification': 'unverified', 'loader': 'public-source-legacy',
        }
        self.build_dir = self.root / 'build'
        self.configuration = 'ChinaServer'
        self.directory = self.build_dir / self.configuration
        self.directory.mkdir(parents=True)
        for name in ('R3nzSkin.dll', 'R3nzSkin_Injector.exe'):
            binary = bytearray(make_pe(b'fixture executable section'))
            struct.pack_into('<H', binary, 0x80 + 22, 0x2022 if name.endswith('.dll') else 0x22)
            (self.directory / name).write_bytes(binary)
        self.info = {
            'source_commit': self.commit, 'source_dirty': False,
            'configuration': self.configuration, 'platform': 'x64',
            'binaries': {name: digest((self.directory / name).read_bytes())
                         for name in ('R3nzSkin.dll', 'R3nzSkin_Injector.exe')},
        }
        self.write_info()

    def write_info(self):
        (self.directory / 'build-info.json').write_text(json.dumps(self.info), encoding='utf-8')

    def package(self, output='output'):
        return package_configuration(self.root, self.build_dir, self.root / output,
                                     self.configuration, self.settings, self.commit)

    def test_round_trip_unicode_docs_and_stable_zip_metadata(self):
        path = self.package()
        manifest = verify_archive(path)
        self.assertEqual(manifest['source_commit'], self.commit)
        self.assertEqual(manifest['runtime_verification'], 'unverified')
        with zipfile.ZipFile(path) as archive:
            self.assertEqual(archive.read('使用说明.md'), (self.root / 'docs' / 'USAGE.md').read_bytes())
        self.assertIn(digest(path.read_bytes()), path.with_suffix('.zip.sha256').read_text())
        self.assertEqual(path.read_bytes(), self.package('second-output').read_bytes())
        with self.assertRaises(FileExistsError):
            self.package()

    def test_missing_binary_is_rejected(self):
        (self.directory / 'R3nzSkin_Injector.exe').unlink()
        with self.assertRaises(OSError):
            self.package()

    def test_stale_commit_dirty_build_and_wrong_configuration_are_rejected(self):
        for field, value in (('source_commit', 'c' * 40), ('source_dirty', True),
                             ('configuration', 'RiotGamesServers'), ('platform', 'x86')):
            old = self.info[field]
            self.info[field] = value
            self.write_info()
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.package()
            self.info[field] = old

    def test_changed_binary_after_build_is_rejected(self):
        path = self.directory / 'R3nzSkin.dll'
        path.write_bytes(path.read_bytes() + b'changed after build')
        with self.assertRaisesRegex(ValueError, '构建后二进制'):
            self.package()

    def test_wrong_binary_architecture_is_rejected_even_with_matching_hash(self):
        path = self.directory / 'R3nzSkin.dll'
        data = bytearray(path.read_bytes())
        struct.pack_into('<H', data, 0x84, 0x14C)
        path.write_bytes(data)
        self.info['binaries'][path.name] = digest(data)
        self.write_info()
        with self.assertRaisesRegex(ValueError, 'x64 PE'):
            self.package()

    def test_tampered_and_extra_archive_members_are_rejected(self):
        source = self.package()
        with zipfile.ZipFile(source) as archive:
            payload = {name: archive.read(name) for name in archive.namelist()}
        for name, value in (('R3nzSkin.dll', b'tampered'), ('../extra.exe', b'unexpected')):
            changed = dict(payload)
            changed[name] = value
            path = self.root / 'changed.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                for member, data in changed.items():
                    archive.writestr(member, data)
            with self.subTest(name=name), self.assertRaises(ValueError):
                verify_archive(path)

    def test_unverified_status_cannot_silently_become_verified(self):
        self.settings['runtime_verification'] = 'verified'
        with self.assertRaisesRegex(ValueError, 'unverified'):
            self.package()


if __name__ == '__main__':
    unittest.main()
