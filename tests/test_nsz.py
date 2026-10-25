"""tools/nsz.py: a compressed package made whole again, byte for byte."""
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from Cryptodome.Cipher import AES
import zstandard

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import nsz

KEY, NONCE = bytes(range(16)), bytes(range(16, 24)) + bytes(8)
SECTIONS = [(0x4000, 0x3010, 1), (0x7010, 0x5ff0, 3)]   # plain, then CTR from an offset not 16-aligned


def ctr(data, at):
    skip = at & 15
    return AES.new(KEY, AES.MODE_CTR, nonce=NONCE[:8], initial_value=at >> 4).encrypt(bytes(skip) + data)[skip:]


def make_nca():
    """A fake NCA: a header, then plain and AES-CTR sections; (encrypted bytes, decrypted body)."""
    body = os.urandom(0x9000)   # 0x4000 .. 0xd000
    out = bytearray(os.urandom(0x4000))
    for offset, size, kind in SECTIONS:
        piece = body[offset - 0x4000:offset - 0x4000 + size]
        out += ctr(piece, offset) if kind == 3 else piece
    return bytes(out), body


def ncz(nca, body, blocks):
    table = b"NCZSECTN" + struct.pack("<Q", len(SECTIONS))
    for offset, size, kind in SECTIONS:
        table += struct.pack("<QQQQ", offset, size, kind, 0) + KEY + NONCE
    if not blocks:
        return nca[:0x4000] + table + zstandard.ZstdCompressor().compress(body)
    exponent, size = 12, 1 << 12
    parts = [body[i:i + size] for i in range(0, len(body), size)]
    packed = [zstandard.ZstdCompressor().compress(p) for p in parts]
    packed = [c if len(c) < len(p) else p for c, p in zip(packed, parts)]
    head = b"NCZBLOCK" + struct.pack("<BBBBIQ", 2, 1, 0, exponent, len(parts), len(body))
    head += struct.pack(f"<{len(parts)}I", *(len(c) for c in packed))
    return nca[:0x4000] + table + head + b"".join(packed)


def pfs0(files):
    strings = b"".join(n.encode() + b"\0" for n, _ in files)
    head = b"PFS0" + struct.pack("<III", len(files), len(strings), 0)
    at = 0
    for name, data in files:
        head += struct.pack("<QQII", at, len(data), strings.index(name.encode() + b"\0"), 0)
        at += len(data)
    return head + strings + b"".join(d for _, d in files)


class NszTests(unittest.TestCase):
    def check(self, blocks):
        nca, body = make_nca()
        cid = hashlib.sha256(nca).hexdigest()[:32]
        ticket = os.urandom(704)
        with tempfile.TemporaryDirectory() as d:
            src, dst = Path(d) / "game.nsz", Path(d) / "game.nsp"
            src.write_bytes(pfs0([(f"{cid}.ncz", ncz(nca, body, blocks)), ("x.tik", ticket)]))
            nsz.convert(src, dst)
            with open(dst, "rb") as f:
                entries = nsz.pfs0_entries(f)
                self.assertEqual([n for n, _, _ in entries], [f"{cid}.nca", "x.tik"])
                f.seek(entries[0][1])
                self.assertEqual(f.read(entries[0][2]), nca)
                f.seek(entries[1][1])
                self.assertEqual(f.read(entries[1][2]), ticket)

    def test_stream(self):
        self.check(blocks=False)

    def test_blocks(self):
        self.check(blocks=True)

    def test_a_wrong_rebuild_stops(self):
        nca, body = make_nca()
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "bad.nsz"
            src.write_bytes(pfs0([("0" * 32 + ".ncz", ncz(nca, body, False))]))
            with self.assertRaises(SystemExit):
                nsz.convert(src, Path(d) / "bad.nsp")


if __name__ == "__main__":
    unittest.main()
