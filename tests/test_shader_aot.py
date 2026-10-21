"""tools/shader_aot.py's scan of data.arc for GPU programs. Made-up containers; no game files."""
from pathlib import Path
import sys
import struct
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import shader_aot  # noqa: E402


class HeaderTests(unittest.TestCase):
    @staticmethod
    def header(pixel=True, flags=0):
        data = bytearray(0x50)
        first = (2 if pixel else 1) | (3 << 5) | ((5 if pixel else 1) << 10) | (3 << 17)
        struct.pack_into('<I', data, 0, first)
        struct.pack_into('<I', data, 0x4C, flags)
        return data

    def test_pixel_output_flags_are_valid(self):
        for flags in range(4):
            self.assertTrue(shader_aot.is_program_header(self.header(flags=flags)))

    def test_common_reserved_bits_are_rejected_in_both_stages(self):
        for pixel in (False, True):
            for word, mask in [(0, 0x01E00000), (3, 0xF0000000), (4, 0x00F00000), (5, 0x00FFFC0F)]:
                for bit in range(32):
                    if not mask >> bit & 1:
                        continue
                    data = self.header(pixel=pixel)
                    value = struct.unpack_from('<I', data, word * 4)[0]
                    struct.pack_into('<I', data, word * 4, value | 1 << bit)
                    self.assertFalse(shader_aot.is_program_header(data), (pixel, word, bit))

    def test_maxwell_bit25_remains_accepted(self):
        data = self.header(pixel=False)
        first = struct.unpack_from('<I', data)[0]
        struct.pack_into('<I', data, 0, first | 1 << 25)
        self.assertTrue(shader_aot.is_program_header(data))

    def test_exhaustive_alignment_finds_unusual_container_offset(self):
        data = bytes(16) + self.header() + bytes(256)
        arc = types.SimpleNamespace(read=lambda *_: data)
        entries = [('eff', (0, len(data), len(data), 0))]
        normal, _ = shader_aot.find_programs(arc, entries=entries)
        exhaustive, _ = shader_aot.find_programs(arc, entries=entries, all_alignments=True)
        self.assertEqual(len(normal), 0)
        self.assertEqual(len(exhaustive), 1)

    def test_byte_audit_finds_unaligned_program_and_minimum_tail(self):
        data = b'x' + self.header(pixel=False) + bytes(16)
        arc = types.SimpleNamespace(read=lambda *_: data)
        entries = [('unknown', (0, len(data), len(data), 0))]
        normal, _ = shader_aot.find_programs(arc, entries=entries, all_alignments=True)
        audit, _ = shader_aot.find_programs(arc, entries=entries, byte_alignments=True)
        self.assertEqual(len(normal), 0)
        self.assertEqual(len(audit), 1)
        self.assertEqual(audit[0][1], data[1:])

    def test_byte_audit_still_rejects_stage_and_reserved_fields(self):
        for pixel in (False, True):
            data = self.header(pixel=pixel) + bytes(32)
            for offset in (1, 7, 16):
                arc = types.SimpleNamespace(read=lambda *_: bytes(offset) + data)
                entries = [('unknown', (0, len(data) + offset, len(data) + offset, 0))]
                found, _ = shader_aot.find_programs(arc, entries=entries, byte_alignments=True)
                self.assertEqual(len(found), 1)
            bad = bytearray(data)
            struct.pack_into('<I', bad, 12, 0xf0000000)
            arc = types.SimpleNamespace(read=lambda *_: bad)
            found, _ = shader_aot.find_programs(arc, entries=[('unknown', (0, len(bad), len(bad), 0))], byte_alignments=True)
            self.assertEqual(found, [])

    def test_each_reserved_pixel_bit_is_rejected(self):
        for bit in range(2, 32):
            self.assertFalse(shader_aot.is_program_header(self.header(flags=1 << bit)))

    def test_vertex_has_a_different_output_layout(self):
        self.assertTrue(shader_aot.is_program_header(self.header(pixel=False, flags=0xFFFFFFFF)))

    def test_truncated_or_mismatched_headers_are_rejected(self):
        self.assertFalse(shader_aot.is_program_header(self.header()[:-1]))
        data = self.header()
        data[1] ^= 1 << 2   # pixel SPH type with a non-pixel shader stage
        self.assertFalse(shader_aot.is_program_header(data))
        self.assertFalse(shader_aot.is_program_header(self.header(), -1))

    def test_header_at_container_offset(self):
        self.assertTrue(shader_aot.is_program_header(b'padding' + self.header(), 7))

    def test_false_match_does_not_split_a_shader_or_add_a_pair(self):
        valid = self.header()
        invalid = self.header(flags=0xFDA66441)
        data = bytes(valid) + bytes(0x100 - len(valid)) + bytes(invalid) + bytes(0x100)
        arc = types.SimpleNamespace(read=lambda off, n: data)
        stats = {}
        with mock.patch.object(shader_aot, 'containers', lambda a: [('nushdb', (0, len(data), len(data), 0))]):
            programs, unread = shader_aot.find_programs(arc, stats)
        self.assertEqual(unread, 0)
        self.assertEqual(len(programs), 1)
        self.assertEqual(programs[0][1], data)
        self.assertEqual(stats, {'invalid_headers': 1})


class EmptyFilesTests(unittest.TestCase):
    def test_empty_files_are_not_unreadable(self):
        """data.arc has a few empty files marked compressed (the same 7 in 11.0.0 and 13.0.5): nothing
        to read in them. Prepare Shaders said "7 files could not be read" for them."""
        reads = []

        def read(off, n):
            reads.append(off)
            return b'\x28\xb5\x2f\xfd broken'[:n]   # (a zstd frame cut short)
        arc = types.SimpleNamespace(read=read)
        found = [('eff', (100, 0, 0, 3)), ('arc', (200, 0, 0, 3)), ('eff', (300, 8, 64, 3))]
        with mock.patch.object(shader_aot, 'containers', lambda a: found):
            programs, unread = shader_aot.find_programs(arc)
        self.assertEqual(unread, 1)   # (only the one whose data is broken)
        self.assertEqual(reads, [300])
        self.assertEqual(programs, [])


class RegionalContainersTests(unittest.TestCase):
    def test_archive_audit_includes_unknown_file_extensions(self):
        archive = types.SimpleNamespace(
            t=struct.pack('<4Q', 0, shader_aot.A.hash40('unlisted'), 0, 0),
            tab={'paths': (0, 32, 1)}, regions=1,
            _entry=lambda table, *_: (0, 0) if table == 'info_indices' else (0, 0, 7, 0),
            _data=lambda index: (123, 16, 16, 0))
        self.assertEqual(shader_aot.containers(archive), [])
        self.assertEqual(len(shader_aot.containers(archive, all_files=True)), 1)

    def test_all_regions_and_shared_data_deduplication(self):
        archive = types.SimpleNamespace(
            t=struct.pack('<4Q', 0, shader_aot.A.hash40('eff'), 0, 0),
            tab={'paths': (0, 32, 1)}, regions=3,
            _entry=lambda table, *_: (0, 0) if table == 'info_indices' else (0, 0, 7, 0x8000),
            _data=lambda index: (100 if index in (7, 9) else 200, 16, 16, 0))
        self.assertEqual(len(shader_aot.containers(archive)), 1)
        regions = shader_aot.containers(archive, all_regions=True)
        self.assertEqual([where[0] for _, where in regions], [100, 200])


if __name__ == '__main__':
    unittest.main()
