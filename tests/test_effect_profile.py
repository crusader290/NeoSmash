"""Profile selection and cache-key safety without game files or a GPU."""
from pathlib import Path
import datetime
import os
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import cc
import prepare_effects
import shader_aot


class EffectProfileTests(unittest.TestCase):
    def test_recent_fighter_selection_excludes_utility_modules_and_is_bounded(self):
        log = '\n'.join('[ro] loaded lua2cpp_' + name + ' at 0x7200000000' for name in
                        ['common', 'item', 'link', 'edge', 'mario', 'notafighter', 'link', 'peach'])
        self.assertEqual(prepare_effects.recent_fighters(log), ['edge', 'mario', 'link', 'peach'])
        self.assertEqual(prepare_effects.recent_fighters(log, limit=2), ['link', 'peach'])

    def test_failed_expansion_keeps_previous_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            tree = Path(temporary)
            manifest = tree / "dump/effect_profiles/whole-game/shaders.txt"
            manifest.parent.mkdir(parents=True)
            previous = b"previous validated inventory\n"
            manifest.write_bytes(previous)
            with mock.patch.object(sys, "argv", ["prepare_effects.py", "--tree", str(tree), "--whole-game"]), \
                 mock.patch.object(shader_aot.A, "Arc", side_effect=RuntimeError("archive unavailable")):
                with self.assertRaisesRegex(RuntimeError, "archive unavailable"):
                    prepare_effects.main()
            self.assertEqual(manifest.read_bytes(), previous)

    def test_compute_ownership_and_failed_residency_readback(self):
        source = r'''
#include "gpu_compute_policy.h"
int main(void) {
 if(compute_write_mask(3, 2, 0)!=2 || compute_write_mask(3, 2, 1)!=7) return 1;
 if(compute_write_mask(0, 0, 1)!=0 || compute_write_mask(16, 0, 1)!=0xffffu) return 2;
 for(unsigned k=0;k<16;k++) {
  if(!compute_readback_needed(1,0,0,k)) return 3;
  if(compute_readback_needed(0,0,0,k)) return 4;
  if(compute_readback_needed(1,1,0,k)) return 5;
  if(compute_readback_needed(1,1,1u<<5,k)!=(k==5)) return 6;
 }
 return 0;
}'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            src, exe = folder / "compute.c", folder / "compute.exe"
            src.write_text(source, encoding="ascii")
            result = subprocess.run([cc.CLANG, "/nologo", "/O2", "/I" + str(ROOT / "runtime"),
                                     str(src), "/Fe" + str(exe)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(exe)], check=True)

    def test_preparation_wait_limit_honors_launcher_and_bounds(self):
        source = r'''
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned long long u64;
#include "gpu_prewarm_driver.h"
int main(void) {
 _putenv_s("SSBU_EFFECT_WAIT_SECONDS", "");
 if(prewarm_wait_seconds()!=180) return 1;
 _putenv_s("SSBU_EFFECT_WAIT_SECONDS", "600");
 if(prewarm_wait_seconds()!=600) return 2;
 _putenv_s("SSBU_EFFECT_WAIT_SECONDS", "3600");
 if(prewarm_wait_seconds()!=3600) return 3;
 _putenv_s("SSBU_EFFECT_WAIT_SECONDS", "9999");
 if(prewarm_wait_seconds()!=3600) return 4;
 _putenv_s("SSBU_EFFECT_WAIT_SECONDS", "0");
 if(prewarm_wait_seconds()!=3) return 5;
 return 0;
}'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            src, exe = folder / "wait.c", folder / "wait.exe"
            src.write_text(source, encoding="ascii")
            result = subprocess.run([cc.CLANG, "/nologo", "/O2", "/I" + str(ROOT / "runtime"),
                                     str(src), "/Fe" + str(exe)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(exe)], check=True)

    def test_played_set_includes_compute_and_excludes_unplayed_and_old_versions(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for version, stage, year in [(10, "cs", 2026), (10, "vs", 2001), (9, "ps", 2026)]:
                file = folder / f"v{version}_{stage}_0123456789abcdef_00000000_0000000000000000.dxbc"
                file.write_bytes(b"DXBC" + bytes(64))
                timestamp = datetime.datetime(year, 1, 1, tzinfo=datetime.timezone.utc).timestamp()
                os.utime(file, (timestamp, timestamp))
            result = prepare_effects.played_names(folder, 10)
            self.assertEqual(result, {"v10_cs_0123456789abcdef_00000000_0000000000000000.dxbc"})

    def test_corrupt_played_shader_is_an_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "v10_cs_0123456789abcdef_00000000_0000000000000000.dxbc").write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                prepare_effects.played_names(folder, 10)

    def test_inventory_can_validate_unplayed_files_without_promoting_them_as_played(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            name = "v10_vs_0123456789abcdef_00000000_0000000000000000.dxbc"
            file = folder / name
            file.write_bytes(b"DXBC" + bytes(64))
            stamp = datetime.datetime(2001, 1, 1, tzinfo=datetime.timezone.utc).timestamp()
            os.utime(file, (stamp, stamp))
            self.assertEqual(prepare_effects.cached_names(folder, 10), {name})
            self.assertEqual(prepare_effects.played_names(folder, 10), set())

    def test_selected_entries_do_not_scan_whole_archive(self):
        archive = types.SimpleNamespace(read=lambda *_: b"")
        with mock.patch.object(shader_aot, "containers", side_effect=AssertionError("full scan")):
            self.assertEqual(shader_aot.find_programs(archive, entries=[]), ([], 0))

    def test_only_the_selected_container_is_read(self):
        import struct
        data = bytearray(0x150)
        struct.pack_into("<I", data, 0, 2 | (3 << 5) | (5 << 10) | (3 << 17))
        reads = []
        def read(offset, size):
            reads.append((offset, size))
            return data
        archive = types.SimpleNamespace(read=read)
        with mock.patch.object(shader_aot, "containers", side_effect=AssertionError("full scan")):
            programs, unread = shader_aot.find_programs(archive, entries=[("nushdb", (1234, len(data), len(data), 0))])
        self.assertEqual(reads, [(1234, len(data))])
        self.assertEqual((len(programs), unread), (1, 0))

    def test_key_names_and_deduplication(self):
        keys = ["vs 0123456789abcdef", "ps fedcba9876543210 00000003"]
        names = prepare_effects.cache_names(keys + keys, 10)
        self.assertEqual(len(names), 2)
        self.assertIn("v10_ps_fedcba9876543210_00000003_0000000000000000.dxbc", names)

    def test_bad_keys_are_rejected(self):
        for key in ["../x", "vs 0123456789abcdef 00000003", "ps 0123456789abcdef", "vs 1234", "vs 0123456789abcdef\nother"]:
            with self.assertRaises(ValueError):
                prepare_effects.cache_names([key], 10)

    def test_renderer_validates_manifest_basename_and_version(self):
        source = r'''
#include "gpu_prewarm_manifest.h"
int main(void) {
 const char *good = "v10_ps_0123456789abcdef_00000003_0000000000000000.dxbc";
 if (!prewarm_name_valid(good, 10) || prewarm_name_valid(good, 11)) return 1;
 const char *bad[] = {"../v10_ps_0123456789abcdef_00000003_0000000000000000.dxbc",
 "v10_vs_1_0_0.dxbc", "v10_xx_0123456789abcdef_00000000_0000000000000000.dxbc",
 "v10_ps_0123456789abcdef_00000003_0000000000000000.dxbc.extra",
 "v10_ps_0123456789abcdef_00000003_0000000000000000.dxbc/../x", ""};
 for (unsigned i=0; i<sizeof bad/sizeof *bad; ++i) if(prewarm_name_valid(bad[i],10)) return 2;
 return 0;
}'''
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            src = folder / "check.c"
            src.write_text(source, encoding="ascii")
            exe = folder / "check.exe"
            result = subprocess.run([cc.CLANG, "/nologo", "/O2", "/I" + str(ROOT / "runtime"), str(src), "/Fe" + str(exe)],
                                    cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
