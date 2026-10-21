# Shader coverage audit — 2026-10-21

## Result

The published whole-game inventory grew from **47,613 to 65,050 entries**:
**17,437 additional covered variants**. Of these, **17,383 DXBC files were newly
compiled**; **54** were already cached from gameplay but missing from the old list.
All 65,050 entries passed D3DReflect and their reflected stage matched their filename:
18,519 vertex, 46,518 pixel and 13 compute variants.

Startup remains a separate observed working set, now **4,539** entries. The full
inventory is not loaded into GPU memory during ordinary experimental startup.

## Search and validation

1. Scanned 84 saved logs and played-cache timestamps. Recovered 44 historical
   gameplay keys absent from startup preparation, including old prewarm hits whose
   usage timestamps were not recorded by earlier builds. No observed key lacked DXBC.
2. Audited **176,004 distinct regional archive files**, all extensions and byte
   alignments: **28.39 GiB unpacked**, 231.94 seconds, zero unreadable files.
3. Compared the exhaustive candidate keys with the old 16-byte-alignment inventory.
   The audit found 18,008 additional candidate variants. The gap was predominantly
   byte alignment within the known `.eff`, `.nushdb` and `.arc` shader containers.
4. Compiled candidates into an isolated cache using six low-priority workers.
   **17,982** variants had supported translations and successful DXBC (599 were
   already cached, 17,383 newly compiled). They all passed D3DReflect.
5. Rejected **26** candidates with unsupported instructions/FXC failures. All 26
   came from outside the known shader-container set; no successfully validated new
   source was found outside that set. These probable false matches and diagnostics
   remain isolated under `dump/effect_profiles/archive-audit/` and were not promoted.
6. Rebuilt the normal whole-game inventory with byte-alignment discovery:
   **26,639 source blobs**, **64,940 static keys**, zero FXC failures and zero
   untranslated keys. Included observed variants and retained 34 previously validated
   entries that the changed source-boundary scan no longer enumerated. No old
   published entry was lost. The final list contains 65,050 entries.

The 17,982 supported candidate count compares against the old static key set;
some were already in the old inventory as observed runtime variants. It therefore
differs from the net inventory increase of 17,437.

## Source changes

- `tools/prepare_effects.py --whole-game` now scans every byte alignment, validates
  and retains the previous published entries, includes historical observed coverage,
  and keeps the previous manifest if expansion fails before publication.
- `tools/shader_aot.py` provides explicit all-file/byte-alignment audit support using
  a native regex prefix search plus the existing full header validation.
- `tools/audit_shader_archive.py` performs the broader archive search separately.
- `tools/shaderaot.c --source-report <file>` records per-program unsupported
  instruction diagnostics without changing normal compilation keys.
- `tools/prepare_startup_shaders.py --scan-history` recovers older exact gameplay
  keys into the persistent startup set. Ordinary startup still reads only recent
  logs plus cache/history, keeping preparation bounded.
- `tools/check_shader_manifest.py` validates DXBC with D3DReflect without GPU object
  creation; `--cache` supports validation of isolated candidates.

**42 focused regression tests passed**, including unaligned/minimum-size discovery,
unknown file extensions, source-report key stability and failed-expansion preservation.
`git diff --check` passed. No game executable/renderer behavior was changed in this
audit, and no save, display driver or existing shader bytecode was overwritten.
New cache files retain their unplayed timestamps until actual game use.

## Outputs and reproduction

Published lists and metadata: `dump/effect_profiles/whole-game/` (`shaders.txt`,
`compilation-list.txt`, `profile.json`, `programs.pack`, `compile.log`). Startup list:
`dump/effect_profiles/startup-observed/shaders.txt`.

Audit evidence: `dump/effect_profiles/archive-audit/scan.json`, `sources.tsv`,
`source-classification.json`, `static.keys`, `additional.keys`,
`supported-additional.keys`, `validated-candidates.txt`, `candidate-compile.log`.
The pre-expansion inventory and pack are preserved in
`build/shader-coverage-before-20261021/`.

From the repository root, with the local Python environment:

```powershell
.\.venv\Scripts\python.exe tools/prepare_startup_shaders.py --scan-history
.\.venv\Scripts\python.exe tools/prepare_effects.py --whole-game
.\.venv\Scripts\python.exe tools/check_shader_manifest.py
```

For another independent all-file search, run `tools/audit_shader_archive.py` first.
Its raw candidates are not a ready-to-publish manifest; inspect source reports and
compile/reflect them separately. `build/compile_archive_candidates.py` records this
audit's isolated compilation procedure.

## Limits

Stored vertex/pixel source discovery does not prove every variant is used. Compute
programs lack the same identifying header and require dispatch dimensions; the
inventory includes 13 observed variants, not every possible compute launch. Unseen
jump-table values, vertex/pixel interfaces and render states can still add runtime
variants. Compiled DXBC is not proof that all native GPU pipelines are ready.
No gameplay FPS/stutter improvement was measured in this audit.
