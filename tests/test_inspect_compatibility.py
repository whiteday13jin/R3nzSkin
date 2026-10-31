"""使用合成 PE 验证离线检查，测试数据不含游戏文件。"""

import struct
import tempfile
import unittest
from pathlib import Path

from tools.inspect_compatibility import compile_pattern, inspect_file, parse_pe, parse_signatures


def make_pe(content: bytes, second: bytes | None = None) -> bytes:
    chunks = [content] if second is None else [content, second]
    data = bytearray(512 + 512 * len(chunks))
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x8664, len(chunks))
    struct.pack_into('<H', data, 0x94, 0xF0)
    struct.pack_into('<H', data, 0x98, 0x20B)
    for index, chunk in enumerate(chunks):
        entry = 0x188 + 40 * index
        data[entry:entry + 8] = b'.text\0\0\0'
        struct.pack_into('<IIII', data, entry + 8, len(chunk), 0x1000 * (index + 1), 512, 512 * (index + 1))
        struct.pack_into('<I', data, entry + 36, 0x60000020)
        start = 512 * (index + 1)
        data[start:start + len(chunk)] = chunk
    return bytes(data)


class CompatibilityTests(unittest.TestCase):
    def test_wildcard_matches_newline_and_overlapping_hits(self):
        self.assertEqual([m.start() for m in compile_pattern('41 ? 41').finditer(b'A\nA\nA')], [0, 2])

    def test_invalid_patterns_are_rejected(self):
        for pattern in ('', '?? ?', 'AA ZZ', 'A?'):
            with self.subTest(pattern=pattern), self.assertRaises(ValueError):
                compile_pattern(pattern)

    def test_source_parser_tracks_all_current_entries(self):
        source = Path(__file__).resolve().parents[1] / 'R3nzSkin' / 'memory.hpp'
        signatures = parse_signatures(source.read_text(encoding='utf-8-sig'))
        self.assertIn('offsets::global::GameClient', signatures)
        self.assertIn('offsets::AIBaseCommon::SkinId', signatures)

    def test_alternatives_comments_and_duplicate_source_targets(self):
        source = '''// &offsets::not_an_entry
        { { "AA ? BB", "CC DD" }, true, false, true, 0, &offsets::global::Player }
        /* &offsets::also_not_an_entry */
        '''
        self.assertEqual(parse_signatures(source),
                         {'offsets::global::Player': ['AA ? BB', 'CC DD']})
        with self.assertRaises(ValueError):
            parse_signatures(source + source)

    def test_source_format_change_cannot_report_partial_success(self):
        with self.assertRaises(ValueError):
            parse_signatures('{ custom_pattern(), &offsets::global::Player }')

    def test_corrupt_pe_and_out_of_bounds_section_are_rejected(self):
        invalid = bytearray(make_pe(b'ABC'))
        struct.pack_into('<I', invalid, 0x188 + 20, len(invalid) + 1)
        for data in (b'', b'MZ' + bytes(80), bytes(invalid), make_pe(b'ABC')[:700]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                parse_pe(data)

    def test_unique_missing_ambiguous_and_alternative_deduplication(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.exe'
            path.write_bytes(make_pe(b'ABCxABCyDEF'))
            result = inspect_file(path, {'twice': ['41 42 43'], 'once': ['44 45 46', '44 ? 46'],
                                         'absent': ['FF EE DD']})
        self.assertEqual(result['summary'], {'missing': 1, 'unique': 1, 'ambiguous': 1})
        self.assertTrue(result['is_x64'])
        self.assertNotIn(directory, str(result))

    def test_no_match_across_section_boundary_and_first_section_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.exe'
            path.write_bytes(make_pe(b'X' * 510 + b'AB', b'CDzzEF'))
            result = inspect_file(path, {'boundary': ['41 42 43 44'], 'second': ['45 46']})
        self.assertEqual(result['signatures'][0]['matches'], 0)
        self.assertEqual(result['signatures'][1]['matches'], 1)
        self.assertEqual(result['signatures'][1]['first_section_matches'], 0)


if __name__ == '__main__':
    unittest.main()
