# Linux status

This handoff was built and tested on Windows. **The Linux build and these local
renderer changes have not been validated on Linux.** Do not infer Linux controller,
performance or graphics correctness from the Windows results.

The repository retains the upstream Linux cross-build tools in `tools/linux/`.
They use generated game code from an existing Windows game tree, a Linux sysroot,
DXVK-native and vkd3d-shader; the upstream Linux runtime uses SDL2. The Windows
installation's SDL3 dependency is separate.

Read [README-LINUX-UPSTREAM.md](README-LINUX-UPSTREAM.md) for the preserved dependency,
setup, launch and packaging instructions. After preparing that toolchain, its build
sequence is:

```text
python tools/linux/build_vkd3d.py linux
python tools/linux/build_linux.py --jobs 16
python tools/linux/package_linux.py --tar
```

These commands are documented from the existing scripts/instructions, not a report
of a successful Linux build in this session. Generated builds contain the user's game
code/data and are not a source-only handoff.

For current Windows play use [README.md](README.md). For continuation context and
source changes read [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) and
[MANIFEST.md](MANIFEST.md). [NOTICE.md](NOTICE.md) retains third-party notices.
