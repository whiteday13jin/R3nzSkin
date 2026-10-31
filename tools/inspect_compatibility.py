"""只读检查磁盘 PE 文件的特征覆盖率，不访问或修改游戏进程。"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path


ENTRY = re.compile(
    r'\{\s*\{\s*(?P<patterns>(?:"[0-9A-Fa-f?\s]+"\s*,?\s*)+)\}'
    r'\s*,\s*(?:true|false)\s*,\s*(?:true|false)\s*,\s*(?:true|false)'
    r'\s*,\s*-?\d+\s*,\s*&(?P<name>offsets::[\w:]+)\s*\}'
)


@dataclass(frozen=True)
class Section:
    index: int
    name: str
    executable: bool
    data: bytes


def parse_signatures(source: str) -> dict[str, list[str]]:
    """直接读取现有 C++ 特征表，格式变化时拒绝静默漏检。"""
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    expected = re.findall(r'&\s*(offsets::[\w:]+)', source)
    entries = list(ENTRY.finditer(source))
    names = [entry['name'] for entry in entries]
    if not expected or sorted(expected) != sorted(names) or len(names) != len(set(names)):
        raise ValueError('特征表格式改变、存在重复项或没有完整解析，停止检查')
    return {
        entry['name']: re.findall(r'"([0-9A-Fa-f?\s]+)"', entry['patterns'])
        for entry in entries
    }


def compile_pattern(pattern: str) -> re.Pattern[bytes]:
    tokens = pattern.split()
    if not tokens or all(token in ('?', '??') for token in tokens):
        raise ValueError('特征不能为空或全部由通配符组成')
    parts = []
    for token in tokens:
        if token in ('?', '??'):
            parts.append(b'.')
        elif re.fullmatch(r'[0-9a-fA-F]{2}', token):
            parts.append(re.escape(bytes.fromhex(token)))
        else:
            raise ValueError(f'无效特征字节：{token}')
    # 前瞻保留重叠命中；DOTALL 允许通配符匹配 0A。
    return re.compile(b'(?=' + b''.join(parts) + b')', re.DOTALL)


def parse_pe(data: bytes) -> tuple[int, list[Section]]:
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('不是有效的 DOS/PE 文件')
    offset = struct.unpack_from('<I', data, 0x3C)[0]
    if offset + 24 > len(data) or data[offset:offset + 4] != b'PE\0\0':
        raise ValueError('PE 头缺失或超出文件范围')
    machine, count = struct.unpack_from('<HH', data, offset + 4)
    optional_size = struct.unpack_from('<H', data, offset + 20)[0]
    table = offset + 24 + optional_size
    if count == 0 or table + count * 40 > len(data):
        raise ValueError('PE 节表为空或超出文件范围')
    sections = []
    for index in range(count):
        entry = table + index * 40
        name = data[entry:entry + 8].rstrip(b'\0').decode('ascii', errors='replace')
        size, pointer = struct.unpack_from('<II', data, entry + 16)
        flags = struct.unpack_from('<I', data, entry + 36)[0]
        if size and (pointer < table + count * 40 or pointer + size > len(data)):
            raise ValueError(f'节 {index} 的原始数据范围无效')
        sections.append(Section(index, name, bool(flags & 0x20000000), data[pointer:pointer + size]))
    return machine, sections


def inspect_file(path: Path, signatures: dict[str, list[str]]) -> dict:
    data = path.read_bytes()
    machine, sections = parse_pe(data)
    executable = [section for section in sections if section.executable]
    results = []
    for name, alternatives in signatures.items():
        # 同一位置被多个备选特征命中时只计一次。
        hits = set()
        for pattern in alternatives:
            matcher = compile_pattern(pattern)
            for section in executable:
                hits.update((section.index, match.start()) for match in matcher.finditer(section.data))
        count = len(hits)
        results.append({
            'name': name,
            'matches': count,
            'status': 'missing' if count == 0 else 'unique' if count == 1 else 'ambiguous',
            'first_section_matches': sum(index == 0 for index, _ in hits),
        })
    return {
        'name': path.name,
        'sha256': hashlib.sha256(data).hexdigest(),
        'size_bytes': len(data),
        'machine': f'0x{machine:04x}',
        'is_x64': machine == 0x8664,
        'sections': [
            {'index': s.index, 'name': s.name, 'executable': s.executable, 'raw_size': len(s.data)}
            for s in sections
        ],
        'summary': {status: sum(item['status'] == status for item in results)
                    for status in ('missing', 'unique', 'ambiguous')},
        'signatures': results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', type=Path, nargs='+', help='磁盘上的 EXE 或 DLL')
    parser.add_argument('--source', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'R3nzSkin' / 'memory.hpp')
    parser.add_argument('--output', type=Path, help='JSON 输出路径；不包含绝对游戏路径或二进制内容')
    args = parser.parse_args()
    try:
        signatures = parse_signatures(args.source.read_text(encoding='utf-8-sig'))
        report = {
            'schema_version': 1,
            'scope': 'on_disk_executable_sections_only',
            'runtime_compatibility': 'unverified',
            'limitation': ('磁盘特征缺失可能来自打包、运行时展开或版本变化；'
                           '命中也不证明对象布局、调用接口、加载能力或账号风险。'),
            'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
            'signature_count': len(signatures),
            'files': [inspect_file(path, signatures) for path in args.files],
        }
        output = json.dumps(report, ensure_ascii=False, indent=2)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output + '\n', encoding='utf-8')
            for item in report['files']:
                print(f"{item['name']}: {item['summary']}")
        else:
            print(output)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, f'检查失败：{error}\n')
    # 0 仅表示检查完成，绝不是游戏兼容或可以加载的证明。
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
