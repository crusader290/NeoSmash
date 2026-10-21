"""Observed working-set selection must not promote speculative AOT variants."""
from pathlib import Path
import datetime
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from prepare_startup_shaders import select_names
import prepare_startup_shaders


class StartupSelectionTests(unittest.TestCase):
    def test_predictive_effects_do_not_pollute_observed_history_or_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            cache = tree / 'dump/shadercache'
            cache.mkdir(parents=True)
            names = [f'v10_vs_{n:016x}_00000000_0000000000000000.dxbc' for n in range(2)]
            stamps = []
            for n, name in enumerate(names):
                file = cache / name
                file.write_bytes(b'DXBC' + bytes(64))
                stamp = datetime.datetime(2026 if n == 0 else 2001, 1, 1,
                                          tzinfo=datetime.timezone.utc).timestamp()
                os.utime(file, (stamp, stamp))
                stamps.append(file.stat().st_mtime)
            profile = tree / 'effects.txt'
            profile.write_text(names[1] + '\n')
            with mock.patch.object(sys, 'argv', ['prepare_startup_shaders.py', '--tree', str(tree),
                                                '--effects-profile', str(profile)]):
                prepare_startup_shaders.main()
            observed = tree / 'dump/effect_profiles/startup-observed/shaders.txt'
            selected = tree / 'dump/effect_profiles/startup-selected/shaders.txt'
            self.assertEqual(observed.read_text().splitlines(), names[:1])
            self.assertEqual(selected.read_text().splitlines(), names)
            self.assertEqual([(cache / name).stat().st_mtime for name in names], stamps)

    def test_observed_previous_and_log_keys_only(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory)
            names = [f"v10_vs_{n:016x}_00000000_0000000000000000.dxbc" for n in range(4)]
            for n, name in enumerate(names):
                file = cache / name
                file.write_bytes(b"DXBC" + bytes(64))
                stamp = datetime.datetime(2026 if n == 0 else 2001, 1, 1,
                                          tzinfo=datetime.timezone.utc).timestamp()
                os.utime(file, (stamp, stamp))
            missing = "v10_vs_ffffffffffffffff_00000000_0000000000000000.dxbc"
            selected = select_names(cache, 10,
                                    [f"[shader-key] {names[1]}\n[shader-key] {missing}\n[shader-key] ../../bad"],
                                    [names[2], missing])
            self.assertEqual(selected, names[:3])
            self.assertNotIn(names[3], selected)


if __name__ == "__main__":
    unittest.main()
