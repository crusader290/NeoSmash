"""Driver-cache warming: which shaders, resuming, and starting again for a new DXVK."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import warm_native_cache as warm


def name(stage, n, version=10):
    return f"v{version}_{stage}_{n:016x}_00000000_0000000000000000.dxbc"


class NativeWarmTests(unittest.TestCase):
    def make_tree(self, directory):
        tree = Path(directory)
        cache = tree / "dump/shadercache"
        cache.mkdir(parents=True)
        whole = [name("vs", 1), name("ps", 2), name("cs", 3), name("ps", 4)]
        for entry in whole:
            (cache / entry).write_bytes(b"DXBC" + bytes(64))
        lists = {"whole-game": whole + [name("ps", 9, version=9)], "startup-selected": [name("ps", 4), name("vs", 1)]}
        for profile, names in lists.items():
            path = tree / "dump/effect_profiles" / profile / "shaders.txt"
            path.parent.mkdir(parents=True)
            path.write_text("\n".join(names) + "\n")
        exe = tree / "build/ssbu.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"MZ")
        (exe.parent / "d3d11.dll").write_bytes(b"dxvk 1")
        (tree / "tools").mkdir()
        (tree / "tools/effects-dxvk.conf").write_text("dxvk.numCompilerThreads = 16\n")
        return tree, whole

    def run_main(self, tree, failed=()):
        batches = []

        def run_batch(exe, batch_file, *rest):
            batches.append(batch_file.read_text().split())
            return (1 if failed else 0), [entry for entry in failed if entry in batches[-1]]

        with mock.patch.object(warm, "run_batch", run_batch), mock.patch.object(warm, "game_running", return_value=False), \
                mock.patch.object(warm, "driver_version", return_value="AMD 1"), \
                mock.patch.object(sys, "argv", ["warm_native_cache.py", "--tree", str(tree), "--batch", "2"]):
            self.assertEqual(warm.main(), 0)
        return batches

    def test_startup_lists_first_without_duplicates_or_other_versions(self):
        with tempfile.TemporaryDirectory() as directory:
            tree, whole = self.make_tree(directory)
            names = warm.wanted_names(tree, 10)
            self.assertEqual(names, [name("ps", 4), name("vs", 1), name("ps", 2), name("cs", 3)])

    def test_resumes_skips_failures_and_restarts_for_a_new_dxvk(self):
        with tempfile.TemporaryDirectory() as directory:
            tree, whole = self.make_tree(directory)
            first = self.run_main(tree, failed=[name("cs", 3)])
            self.assertEqual(sum(first, []), [name("ps", 4), name("vs", 1), name("ps", 2), name("cs", 3)])
            self.assertEqual(max(len(batch) for batch in first), 2)
            # only what did not succeed is tried again
            self.assertEqual(self.run_main(tree), [[name("cs", 3)]])
            self.assertEqual(self.run_main(tree), [])
            # a different DXVK has an empty driver cache of its own
            (tree / "build/d3d11.dll").write_bytes(b"dxvk 2")
            self.assertEqual(len(sum(self.run_main(tree), [])), 4)


if __name__ == "__main__":
    unittest.main()
