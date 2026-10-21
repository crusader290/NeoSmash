"""Validate a preparation manifest's DXBC through Windows D3DReflect, without GPU objects."""
import argparse
import ctypes as C
import json
from pathlib import Path
import re
import uuid

import shader_aot as aot


def check(tree, manifest, cache=None):
    cache = cache or tree / "dump/shadercache"
    reflect = C.WinDLL("d3dcompiler_47.dll").D3DReflect
    reflect.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.POINTER(C.c_void_p)]
    reflect.restype = C.c_long
    iid = C.create_string_buffer(uuid.UUID("8d536ca1-0cca-4956-a837-786963755584").bytes_le)
    counts = dict(vs=0, ps=0, cs=0)
    names = manifest.read_text(encoding="ascii").splitlines()
    if not names or len(names) != len(set(names)):
        raise ValueError("manifest is empty or contains duplicate entries")
    pattern = re.compile(rf"v{aot.cache_version()}_(vs|ps|cs)_[0-9a-f]{{16}}_[0-9a-f]{{8}}_[0-9a-f]{{16}}\.dxbc")
    for name in names:
        match = pattern.fullmatch(name)
        if not match:
            raise ValueError(f"invalid shader basename: {name}")
        data = (cache / name).read_bytes()
        buffer, obj = C.create_string_buffer(data), C.c_void_p()
        hr = reflect(buffer, len(data), iid, C.byref(obj))
        if hr != 0 or not obj.value:
            raise ValueError(f"D3DReflect rejected {name}: {hr & 0xffffffff:#x}")
        vtable = C.cast(obj, C.POINTER(C.POINTER(C.c_void_p))).contents
        try:
            desc = C.create_string_buffer(1024)
            get_desc = C.WINFUNCTYPE(C.c_long, C.c_void_p, C.c_void_p)(vtable[3])
            if get_desc(obj, desc) != 0:
                raise ValueError(f"cannot reflect shader descriptor: {name}")
            stage = match[1]
            version = C.cast(desc, C.POINTER(C.c_uint32)).contents.value
            if version >> 16 != dict(ps=0, vs=1, cs=5)[stage]:
                raise ValueError(f"shader stage mismatch: {name}")
            counts[stage] += 1
        finally:
            C.WINFUNCTYPE(C.c_ulong, C.c_void_p)(vtable[2])(obj)
    return dict(shaders=len(names), by_stage=counts, result="all bytecode reflected and stages matched")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=Path(aot.ROOT))
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--cache", type=Path, help="alternate cache, for validating isolated candidates")
    args = parser.parse_args()
    tree = args.tree.resolve()
    manifest = args.manifest or tree / "dump/effect_profiles/whole-game/shaders.txt"
    print(json.dumps(check(tree, manifest, args.cache)), flush=True)


if __name__ == "__main__":
    main()
