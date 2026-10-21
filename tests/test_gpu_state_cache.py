"""Exercise the production descriptor-cache macro with native D3D descriptor types."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import cc


@unittest.skipUnless(os.name == 'nt', 'native Windows SDK descriptor types required')
class StateCacheTests(unittest.TestCase):
    def test_collisions_capacity_overflow_and_diagnostic_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'state_cache.exe'
            obj = Path(directory) / 'state_cache.obj'
            result = subprocess.run(
                [cc.CLANG, '/nologo', '/O2', *cc.CFLAGS,
                 str(ROOT / 'tests/test_gpu_state_cache.c'),
                 '/Fe:' + str(exe), '/Fo:' + str(obj)],
                capture_output=True, text=True, timeout=60, cwd=directory)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe), '--check'], capture_output=True,
                                    text=True, timeout=30, cwd=directory)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('100000 mixed hits, capacity/overflow and mode switch passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
