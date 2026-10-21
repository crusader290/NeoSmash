"""Native correctness checks for composed-upload snapshot invalidation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import cc


class UploadSnapshotTests(unittest.TestCase):
    def test_exact_bytes_address_size_gpu_generation_and_bypass(self):
        source = r'''
#include "gpu_upload_snapshot.h"
#include <assert.h>
int main(void) {
 GpuUploadSnapshot s = {0};
 unsigned char data[GPU_SNAPSHOT_MAX + 1] = {1,2,3,4};
 assert(!gpu_snapshot_matches(&s, 0x1000, 4, 1, data));
 gpu_snapshot_commit(&s, 0x1000, 4, 1, data);
 assert(gpu_snapshot_matches(&s, 0x1000, 4, 1, data));
 assert(!gpu_snapshot_matches(&s, 0x2000, 4, 1, data));
 assert(!gpu_snapshot_matches(&s, 0x1000, 3, 1, data));
 assert(!gpu_snapshot_matches(&s, 0x1000, 4, 2, data));
 data[2] ^= 1;
 assert(!gpu_snapshot_matches(&s, 0x1000, 4, 1, data));
 gpu_snapshot_commit(&s, 0x1000, 4, 2, data);
 assert(gpu_snapshot_matches(&s, 0x1000, 4, 2, data));
 gpu_snapshot_commit(&s, 0x1000, sizeof data, 2, data);
 assert(!gpu_snapshot_matches(&s, 0x1000, 4, 2, data));
 assert(!gpu_snapshot_matches(&s, 0x1000, sizeof data, 2, data));
 gpu_snapshot_commit(&s, 0x1000, GPU_SNAPSHOT_MAX, 3, data);
 assert(gpu_snapshot_matches(&s, 0x1000, GPU_SNAPSHOT_MAX, 3, data));
 data[GPU_SNAPSHOT_MAX - 1] ^= 1;
 assert(!gpu_snapshot_matches(&s, 0x1000, GPU_SNAPSHOT_MAX, 3, data));
 gpu_snapshot_commit(&s, 0, 0, 3, NULL);
 assert(!gpu_snapshot_matches(&s, 0x1000, GPU_SNAPSHOT_MAX, 3, data));
 free(s.bytes);
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
