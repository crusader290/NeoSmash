"""Small made-up packs exercise the built native helper, when it is available."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parent.parent / 'build/shaderaot.exe'
ROOT = TOOL.parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import cc


@unittest.skipUnless(os.name == 'nt' and TOOL.is_file(), 'build shaderaot.exe to run native tests')
class NativeWorkerTests(unittest.TestCase):
    def test_source_report_records_supported_program_without_changing_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            program = self.program()
            pack, report = folder / 'one.pack', folder / 'sources.tsv'
            pack.write_bytes(b'SAOT' + struct.pack('<4I', 1, 0, len(program), 0) + program)
            command = [str(TOOL), str(pack), str(folder), '0', '1', '--dry']
            original = subprocess.run(command, capture_output=True, text=True, timeout=30)
            audited = subprocess.run(command + ['--source-report', str(report)], capture_output=True, text=True, timeout=30)
            self.assertEqual(audited.returncode, 0, audited.stderr)
            self.assertEqual(audited.stdout, original.stdout)
            fields = report.read_text().splitlines()[0].split('\t')
            self.assertEqual(fields[:2], ['0', 'vs'])
            self.assertEqual(fields[4], '')
            self.assertGreater(int(fields[3]), 0x50)

    def test_all_interface_masks_and_exact_key_sets(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'sets.exe'
            result = subprocess.run([cc.CLANG, '/nologo', '/O2', *cc.CFLAGS,
                                     str(ROOT / 'tests/test_shader_aot_sets.c'), str(ROOT / 'runtime/gpu_shader.c'),
                                     '/Fe' + str(exe), '/link', 'd3dcompiler.lib'],
                                    cwd=folder, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('low-bit keys, stage separation and growth passed', result.stdout)

    @staticmethod
    def program(pixel=False, padding=b'', invalid=False):
        data = bytearray(0x60)
        struct.pack_into('<I', data, 0, 0x65462 if pixel else 0x60461)
        struct.pack_into('<Q', data, 0x58, 0xE30000000007000F)  # unconditional EXIT after scheduling word
        if pixel:
            struct.pack_into('<I', data, 0x48, 15)
        if invalid:
            struct.pack_into('<I', data, 0x4C, 4)
        return data + padding

    def run_pack(self, records, workers):
        with tempfile.TemporaryDirectory() as folder:
            pack = bytearray(b'SAOT' + struct.pack('<I', len(records)))
            for stage, program, pairs in records:
                pack += struct.pack('<III', stage, len(program), len(pairs))
                pack += struct.pack(f'<{len(pairs)}I', *pairs) + program
            path = Path(folder) / 'programs.pack'
            path.write_bytes(pack)

            def run(part):
                result = subprocess.run([str(TOOL), str(path), folder, str(part), str(workers), '--dry'],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result.stdout.splitlines(), result.stderr

            with ThreadPoolExecutor(workers) as pool:
                return list(pool.map(run, range(workers)))

    def test_duplicate_programs_have_one_owner_and_identical_coverage(self):
        records = [(0, self.program(padding=b'\0' * 16), []),
                   (0, self.program(padding=b'padding differs'), []),
                   (1, self.program(pixel=True, padding=b'\0' * 16), [0]),
                   (1, self.program(pixel=True, padding=b'other padding'), [1])]
        single = self.run_pack(records, 1)[0][0]
        partitioned = [key for keys, _ in self.run_pack(records, 4) for key in keys]
        self.assertEqual(len(single), 2)
        self.assertEqual(set(single), set(partitioned))
        self.assertEqual(len(partitioned), len(set(partitioned)))

    def test_invalid_pixel_header_is_rejected_before_translation(self):
        keys, stderr = self.run_pack([(1, self.program(pixel=True, invalid=True), [])], 1)[0]
        self.assertEqual(keys, [])
        self.assertIn('ignored invalid SPH in pack record 0', stderr)

    def test_invalid_common_header_is_rejected_before_translation(self):
        for pixel in (False, True):
            program = self.program(pixel=pixel)
            struct.pack_into('<I', program, 12, 0x80000000)
            keys, stderr = self.run_pack([(int(pixel), program, [])], 1)[0]
            self.assertEqual(keys, [])
            self.assertIn('ignored invalid SPH', stderr)


if __name__ == '__main__':
    unittest.main()
