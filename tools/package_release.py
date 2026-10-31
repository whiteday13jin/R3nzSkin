"""生成并校验版本绑定的源码构建包；不运行打包的程序。"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import zipfile
from pathlib import Path

try:
    from tools.inspect_compatibility import parse_pe
except ModuleNotFoundError:
    from inspect_compatibility import parse_pe

CONFIGURATIONS = ('ChinaServer', 'RiotGamesServers')
BINARIES = ('R3nzSkin.dll', 'R3nzSkin_Injector.exe')
PAYLOAD = (*BINARIES, 'Launch.cmd', '使用说明.md', 'LICENSE')
CONTENTS = {*PAYLOAD, 'manifest.json', 'CHECKSUMS.sha256'}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding='utf-8-sig'))


def validate_settings(settings: dict) -> None:
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', settings['version']):
        raise ValueError('发布版本必须为三个数字段，可附加预发布后缀')
    if not re.fullmatch(r'[0-9a-f]{40}', settings['upstream_commit']):
        raise ValueError('上游必须绑定完整提交号')
    if settings['runtime_verification'] != 'unverified' or settings['loader'] != 'public-source-legacy':
        raise ValueError('本发布流程仅支持已定义的公开源码加载器，运行验证状态必须保持 unverified')


def validate_binary(name: str, data: bytes) -> None:
    machine, _ = parse_pe(data)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    flags = struct.unpack_from('<H', data, pe + 22)[0]
    if machine != 0x8664 or bool(flags & 0x2000) != name.endswith('.dll'):
        raise ValueError(f'{name} 不是预期类型的 x64 PE 文件')


def verify_archive(path: Path) -> dict:
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(names) != len(set(names)) or set(names) != CONTENTS:
            raise ValueError('发布包缺少文件、包含重复项或混入额外文件')
        if any(entry.file_size > 64 * 1024 * 1024 for entry in entries):
            raise ValueError('发布包存在超出预期大小的文件')
        data = {name: archive.read(name) for name in names}
    manifest = json.loads(data['manifest.json'])
    if manifest['schema_version'] != 1 or manifest['configuration'] not in CONFIGURATIONS:
        raise ValueError('发布清单版本或配置无效')
    validate_settings(manifest)
    if manifest['platform'] != 'x64' or not re.fullmatch(r'[0-9a-f]{40}', manifest['source_commit']):
        raise ValueError('发布清单缺少有效源码提交或平台信息')
    if set(manifest['files']) != set(PAYLOAD):
        raise ValueError('发布清单没有覆盖全部有效载荷')
    for name, record in manifest['files'].items():
        if digest(data[name]) != record['sha256'] or len(data[name]) != record['size_bytes']:
            raise ValueError(f'发布清单校验失败：{name}')
    expected_checksums = ''.join(f'{digest(data[name])}  {name}\n' for name in sorted(data) if name != 'CHECKSUMS.sha256')
    if data['CHECKSUMS.sha256'].decode('utf-8') != expected_checksums:
        raise ValueError('包内 SHA-256 清单不一致')
    for name in BINARIES:
        validate_binary(name, data[name])
    return manifest


def package_configuration(root: Path, build_dir: Path, output_dir: Path,
                          configuration: str, settings: dict, source_commit: str) -> Path:
    validate_settings(settings)
    if configuration not in CONFIGURATIONS or not re.fullmatch(r'[0-9a-f]{40}', source_commit):
        raise ValueError('构建配置或源码提交号无效')
    directory = build_dir / configuration
    info = read_json(directory / 'build-info.json')
    if (info['source_commit'] != source_commit or info['source_dirty'] is not False
            or info['configuration'] != configuration or info['platform'] != 'x64'):
        raise ValueError('构建记录与当前提交、配置不一致，或产物来自未提交源码')
    payload = {}
    for name in BINARIES:
        data = (directory / name).read_bytes()
        if digest(data) != info['binaries'][name]:
            raise ValueError(f'构建后二进制发生变化：{name}')
        validate_binary(name, data)
        payload[name] = data
    payload['Launch.cmd'] = (root / 'packaging' / 'Launch.cmd').read_bytes()
    payload['使用说明.md'] = (root / 'docs' / 'USAGE.md').read_bytes()
    payload['LICENSE'] = (root / 'LICENSE').read_bytes()
    manifest = {
        'schema_version': 1, **settings, 'source_commit': source_commit,
        'configuration': configuration, 'platform': 'x64',
        'files': {name: {'sha256': digest(data), 'size_bytes': len(data)} for name, data in sorted(payload.items())},
    }
    payload['manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    payload['CHECKSUMS.sha256'] = ''.join(f'{digest(payload[name])}  {name}\n' for name in sorted(payload)).encode('utf-8')
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / f"R3nzSkin-{settings['version']}-{configuration}-x64.zip"
    # 不覆盖已存在的发布包，避免同版本产物在不知情时发生变化。
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    verify_archive(path)
    path.with_suffix(path.suffix + '.sha256').write_text(f'{digest(path.read_bytes())}  {path.name}\n', encoding='utf-8')
    return path


def git_output(root: Path, *arguments: str) -> str:
    return subprocess.check_output(['git', '-C', str(root), *arguments], text=True, encoding='utf-8').strip()


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    package = commands.add_parser('package')
    package.add_argument('--configurations', nargs='+', choices=CONFIGURATIONS, default=list(CONFIGURATIONS))
    package.add_argument('--build-dir', type=Path, default=root / 'artifacts' / 'build')
    package.add_argument('--output-dir', type=Path, default=root / 'artifacts' / 'release')
    verify = commands.add_parser('verify')
    verify.add_argument('archives', type=Path, nargs='+')
    args = parser.parse_args()
    try:
        if args.command == 'verify':
            for path in args.archives:
                manifest = verify_archive(path)
                print(f"校验通过：{path.name} / {manifest['source_commit']}")
            return 0
        if git_output(root, 'status', '--porcelain'):
            raise ValueError('打包要求干净的 Git 工作区')
        settings = read_json(root / 'release.json')
        validate_settings(settings)
        commit = git_output(root, 'rev-parse', 'HEAD')
        directory = args.output_dir / settings['version']
        expected_paths = [directory / f"R3nzSkin-{settings['version']}-{item}-x64.zip" for item in args.configurations]
        if any(path.exists() or path.with_suffix('.zip.sha256').exists() for path in expected_paths):
            raise ValueError('同版本发布文件已存在，请选择新的输出目录或递增版本')
        for item in args.configurations:
            print(package_configuration(root, args.build_dir, directory, item, settings, commit))
        notes = (
            f"# R3nzSkin {settings['version']}\n\n"
            f"源码提交：`{commit}`；上游基线：`{settings['upstream_commit']}`。\n\n"
            f"目标游戏补丁：{settings['game_patch']}；平台：Windows x64。\n\n"
            "包内包含当前公开源码构建的 DLL 和历史启动器、中文使用说明、版本清单及 SHA-256。\n\n"
            "**运行能力尚未验证。此包不包含上游未开源加载器，不代表恢复国服支持。**\n\n"
            "启动器停止调用旧仓库自动更新，保留稳定文件名，按自身目录加载配套 DLL。\n"
        )
        (directory / 'release-notes.md').write_text(notes, encoding='utf-8')
    except (OSError, ValueError, KeyError, TypeError, struct.error, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(2, f'发布检查失败：{error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
