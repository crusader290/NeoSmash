"""The shader container index the renderer reads (runtime/gpu_container_prep.h)."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import container_index as ci


def name(stage, h, mask=0, version=10):
    return f"v{version}_{stage}_{h:016x}_{mask:08x}_0000000000000000.dxbc"


class ContainerIndexTests(unittest.TestCase):
    def test_round_trip_sorted_by_offset(self):
        names = [name("vs", 1), name("ps", 2, 0x7f), name("ps", 2, 0x0f)]
        containers = [(0x9000, 0x200, [1, 2]), (0x1000, 0x80, [0, 1])]
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "index.bin"
            ci.write_index(path, 10, names, containers)
            version, read_names, read = ci.read_index(path)
            raw = path.read_bytes()
        self.assertEqual(version, 10)
        self.assertEqual(read_names, names)
        self.assertEqual(read, [(0x1000, 0x80, [names[0], names[1]]), (0x9000, 0x200, [names[1], names[2]])])
        # what the C reader expects: a 20-byte head, 64-byte names, 24-byte containers, u32 references
        self.assertEqual(raw[:4], b"SCX1")
        self.assertEqual(struct.unpack_from("<4I", raw, 4), (10, 2, 3, 4))
        self.assertEqual(len(raw), 20 + 3 * 64 + 2 * 24 + 4 * 4)

    def test_every_mask_of_a_program_belongs_to_it(self):
        lines = [name("vs", 1), name("ps", 2, 0x7f), name("ps", 2, 0x0f), name("ps", 3, version=9), "junk"]
        got = ci.names_by_program(lines, 10)
        self.assertEqual(got[("vs", f"{1:016x}")], [name("vs", 1)])
        self.assertEqual(sorted(got[("ps", f"{2:016x}")]), sorted([name("ps", 2, 0x7f), name("ps", 2, 0x0f)]))
        self.assertNotIn(("ps", f"{3:016x}"), got)

    def test_long_names_refused(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):
                ci.write_index(Path(d) / "i.bin", 10, ["x" * 64], [])


if __name__ == "__main__":
    unittest.main()
