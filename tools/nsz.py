"""NSZ to NSP: the NCAs of a compressed package (.nsz) made whole again, checked against their names.

An .nsz is an .nsp whose large NCAs are stored as NCZ: the NCA's first 0x4000 bytes as they are
(its header), a table of the NCA's sections with the key and counter of each, then the rest of the
NCA decrypted and compressed with zstd - one stream, or blocks ("NCZBLOCK"). This decompresses
each, encrypts its sections again (AES-128-CTR; counter: the section's upper 8 bytes, then the
NCA offset / 16), and writes an .nsp with the same files in the same order. An NCA's name is the
first half of its SHA-256, so every NCA rebuilt is checked byte for byte; a mismatch stops it.

    python tools/nsz.py <in.nsz> <out.nsp>

Needs zstandard and pycryptodomex (requirements.txt). No keys: the NCZ carries what it needs.
"""
import argparse
import hashlib
import io
import os
from pathlib import Path
import struct
import sys
import time

from Cryptodome.Cipher import AES
import zstandard

HEADER = 0x4000
CHUNK = 16 << 20


def pfs0_entries(f):
    """[(name, offset in the file, size)] of a PFS0 package."""
    f.seek(0)
    head = f.read(16)
    if head[:4] != b"PFS0":
        raise SystemExit("not an NSP/NSZ (no PFS0 header)")
    count, strsize = struct.unpack_from("<II", head, 4)
    table = [struct.unpack("<QQII", f.read(24)) for _ in range(count)]
    strings = f.read(strsize)
    data = 16 + 24 * count + strsize
    return [(strings[n:strings.index(b"\0", n)].decode("utf-8"), data + off, size) for off, size, n, _ in table]


class Bounded(io.RawIOBase):
    """The bytes [start, end) of a file, read as a file of their own (a package entry: a zstd
    reader reading across frames must not run on into the next entry)."""

    def __init__(self, f, start, end):
        self.f, self.at, self.end = f, start, end

    def readable(self):
        return True

    def readinto(self, buf):
        n = min(len(buf), self.end - self.at)
        if n <= 0:
            return 0
        self.f.seek(self.at)
        data = self.f.read(n)
        buf[:len(data)] = data
        self.at += len(data)
        return len(data)


class Sections:
    """The NCZ section table: encrypts plain NCA bytes at their NCA offset."""

    def __init__(self, entries):
        self.entries = sorted(entries)   # (offset, size, crypto type, key, counter)

    def encrypt(self, at, data):
        out = bytearray(data)
        end = at + len(data)
        for offset, size, kind, key, counter in self.entries:
            lo, hi = max(at, offset), min(end, offset + size)
            if lo >= hi or kind not in (3, 4):   # 1: plain; 3: CTR; 4: BKTR (CTR, its own counter)
                continue
            skip = lo & 15
            cipher = AES.new(key, AES.MODE_CTR, nonce=counter[:8], initial_value=lo >> 4)
            piece = cipher.encrypt(bytes(skip) + bytes(out[lo - at:hi - at]))[skip:]
            out[lo - at:hi - at] = piece
        return bytes(out)


def plain_body(f, start, end):
    """The decompressed NCA after its header, from an NCZ entry whose section table ends at
    `start` and which ends at `end`."""
    f.seek(start)
    if f.read(8) == b"NCZBLOCK":
        version, kind, _unused, exponent, count = struct.unpack("<BBBBI", f.read(8))
        total = struct.unpack("<Q", f.read(8))[0]
        sizes = struct.unpack(f"<{count}I", f.read(4 * count))
        block = 1 << exponent
        left = total
        dctx = zstandard.ZstdDecompressor()
        for size in sizes:
            want = min(block, left)
            raw = f.read(size)
            yield dctx.decompress(raw, max_output_size=want) if size < want else raw
            left -= want
        return
    reader = zstandard.ZstdDecompressor().stream_reader(Bounded(f, start, end), read_across_frames=True)
    while True:
        piece = reader.read(CHUNK)
        if not piece:
            return
        yield piece


def write_nca(f, offset, size, out, name):
    """One NCZ entry of the package as its NCA, into `out`; returns its SHA-256."""
    sha = hashlib.sha256()
    f.seek(offset)
    header = f.read(HEADER)
    if f.read(8) != b"NCZSECTN":
        raise SystemExit(f"{name}: not an NCZ (no section table)")
    count = struct.unpack("<Q", f.read(8))[0]
    sections = Sections([(o, s, k, key, ctr) for o, s, k, _pad, key, ctr in
                         (struct.unpack("<QQQQ16s16s", f.read(64)) for _ in range(count))])
    out.write(header)
    sha.update(header)
    at = HEADER
    for piece in plain_body(f, f.tell(), offset + size):
        piece = sections.encrypt(at, piece)
        out.write(piece)
        sha.update(piece)
        at += len(piece)
    return sha, at


def convert(src, dst):
    start = time.monotonic()
    with open(src, "rb") as f:
        entries = pfs0_entries(f)
        names = [n[:-4] + ".nca" if n.endswith(".ncz") else n for n, _, _ in entries]
        # The output's header first, with sizes filled in once the NCAs are rebuilt.
        strings = b"".join(n.encode("utf-8") + b"\0" for n in names)
        strings += bytes((-(16 + 24 * len(names) + len(strings))) % 0x20)
        data0 = 16 + 24 * len(names) + len(strings)
        partial = Path(str(dst) + ".partial")
        sizes = []
        with open(partial, "wb") as out:
            out.write(bytes(data0))
            for (name, offset, size), new in zip(entries, names):
                if name.endswith(".ncz"):
                    print(f"  {new}: decompressing {size / 2**30:.2f} GB", flush=True)
                    sha, written = write_nca(f, offset, size, out, name)
                    if sha.hexdigest()[:32] != new[:32]:
                        raise SystemExit(f"{new}: rebuilt NCA does not match its name (SHA-256 {sha.hexdigest()[:32]})")
                    print(f"  {new}: {written / 2**30:.2f} GB, SHA-256 matches its name", flush=True)
                    sizes.append(written)
                else:   # (stored as it is: an NCA among them is checked against its name too)
                    f.seek(offset)
                    left, sha = size, hashlib.sha256()
                    while left:
                        piece = f.read(min(CHUNK, left))
                        out.write(piece)
                        sha.update(piece)
                        left -= len(piece)
                    if new.endswith(".nca") and sha.hexdigest()[:32] != new[:32]:
                        raise SystemExit(f"{new}: stored NCA does not match its name")
                    sizes.append(size)
            out.seek(0)
            out.write(b"PFS0" + struct.pack("<III", len(names), len(strings), 0))
            at, name_at = 0, 0
            for n, size in zip(names, sizes):
                out.write(struct.pack("<QQII", at, size, name_at, 0))
                at += size
                name_at += len(n.encode("utf-8")) + 1
            out.write(strings)
        os.replace(partial, dst)
    print(f"{dst}: {len(names)} files in {time.monotonic() - start:.0f} s")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("nsz")
    ap.add_argument("nsp")
    a = ap.parse_args()
    if os.path.exists(a.nsp):
        raise SystemExit(f"{a.nsp} exists already")
    convert(a.nsz, a.nsp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
