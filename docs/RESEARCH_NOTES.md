# External research notes

Last reviewed: 2026-11-07

This document records public research that can accelerate the Android ARM64 GT4 port. Links point to upstream projects; this repository does not copy game assets, BIOS files, or copyrighted GT4 data.

## 1. GT4 executable and game-engine research

### gustavocanepa/gt4-decomp
- URL: https://github.com/gustavocanepa/gt4-decomp
- License: MIT (verify the upstream LICENSE before reusing individual files).
- Scope: matching decompilation of the GT4 PS2 executable for NTSC-U SCUS-97328 v1.01.
- Useful findings documented upstream:
  - Retail `CORE.GT4` uses a raw-DEFLATE payload and is not described as having an encryption layer.
  - The executable is loaded at EE address `0x00100000`; entry point is `0x00100008`.
  - The repository documents engine architecture, classes, script-engine integration, runtime types, and function coverage.
  - Upstream README reports approximately 98% of functions matched in November 2026.
- Limitations:
  - This is a matching decompilation, not a finished Android runtime.
  - It targets a specific GT4 disc revision; addresses and layouts must not be assumed to match every region/revision.
  - It requires the user's own legally obtained game executable for its build/verification workflow.
- Use in this project:
  1. Treat its format notes and knowledge documents as the primary GT4-specific reverse-engineering reference.
  2. Compare our CORE parser against the documented header, section layout, and entry point.
  3. Use subsystem/class names to plan compatibility work.
  4. Do not bulk-copy source into this Android project without a deliberate license and build-compatibility review.

### OpenAdhoc
- URL: https://github.com/Nenkai/OpenAdhoc
- Scope: research/reimplementation of the Adhoc script VM used by Gran Turismo titles.
- Why it matters: the GT4 decompilation project notes that much of the game's logic is in Adhoc scripts, so an eventual port needs script VM compatibility as well as executable loading.
- Use: study the VM format and runtime behavior after the EE loader and core runtime foundations are stable. Confirm its current license and revision before reuse.


### gustavocanepa/gt4-native
- URL: https://github.com/gustavocanepa/gt4-native
- License: GPL-3.0 for the port's original code; imported upstream GT4 decompilation files retain MIT notices.
- Scope: experimental source-level native PC port built on `gt4-decomp`, explicitly not a PS2 emulator or static recompiler.
- Useful direction:
  - Guest-memory compatibility tools, ELF inspection, ABI experiments, and an automated pinned-source import/triage workflow.
  - Its documented approach adapts selected original GT4 functions to a host runtime rather than emulating all PS2 hardware.
- Limitations:
  - Upstream explicitly says no playable build exists.
  - It targets PC today; its host ABI, renderer, audio and platform layers are not an Android implementation.
- Use in this project:
  - Study the guest-memory model, source provenance pipeline, ABI checks and test strategy.
  - Evaluate whether a small, isolated host-compatible function can be adapted for Android after reviewing its GPL-3.0 obligations.
  - Do not import the whole repository and describe it as a working port.

### Nenkai/OpenAdhoc
- URL: https://github.com/Nenkai/OpenAdhoc
- License: GPL-3.0.
- Scope: open-source reimplementation of Gran Turismo's proprietary Adhoc scripting language; its README describes scripts as carrying much of the games' logic while the executable provides the engine and script-facing libraries.
- Use: likely important for a source-level GT4 port because engine executable code alone is not the complete game logic. Study script VM semantics and the GT4-specific script coverage.
- Limitations: it is a scripting-language reimplementation and modding/reverse-engineering project, not a complete game runtime or Android port. GPL-3.0 terms must be reviewed before code reuse.


### Native-port milestone and validation model
The upstream GT4 Native roadmap defines separate acceptance tracks for native game code (G6), renderer (R1), sound (A1), and platform services (P1). It proposes comparing deterministic reference sessions at multiple layers (state, calls/cycles, graphics commands/frames, and audio) rather than treating a successful compile as proof of correctness. This is a useful validation model for our Android port, even though the upstream roadmap is for PC and has no playable build.


## 1A. Concrete Android reference: GT3 Revisited (user-provided APK)

### Upstream source
- URL: https://github.com/matt-pulsipher/GT3-revisited
- License: MIT; retain upstream notices for any reused files.
- This is a native reimplementation of the GT3 engine, not a PS2 emulator. It reads the user's ISO/CHD at runtime and does not distribute game assets.
- Its CMake architecture separates a portable C++ engine library (`gt3r_engine`) from the Android SDL entry target (`libmain.so`). Android uses SDL3 Java glue plus NDK/CMake.
- Android documentation describes Storage Access Framework disc selection, touch controls, physical controllers, tilt steering, dynamic resolution and Vulkan rendering.
- Its format documentation covers the GT3 boot executable, CORE.GT3 wrapper, GT3.VOL archive, texture formats and GS-memory texture handling.

### Inspection of the APK supplied for this project
The uploaded `gt3r-android.apk` was inspected locally. It contains native libraries for `arm64-v8a` and `x86_64`. The ARM64 native binary exposes C++ symbols for application lifecycle, disc opening, VFS, screen registry, touch overlay/input manager, audio, GPU context, render scaling and game startup. This is consistent with the upstream SDL/C++ architecture; it does not mean the GT3 game runtime can be reused directly for GT4.

### How we should use it
1. Adopt the same product architecture: native C++ engine, thin Android/SDL activity, user-picked disc image, runtime VFS, touch/controller input, native audio and GPU renderer.
2. Use GT3 Revisited as a reference for Android packaging, file-picker integration, UI flow, input configuration, render scaling, testing and CI.
3. Reuse only small, separable MIT-licensed platform utilities after checking dependencies and notices.
4. Do not copy GT3-specific game logic, GT3.VOL parser assumptions, GT3 physics, car/course formats or GT3 executable layouts into GT4. GT4 has its own executable and asset-container differences.
5. Prefer GT4-specific decompilation and format research for the game engine. GT3 Revisited is a platform/architecture reference, not a ready-made GT4 engine.

### Hardware caution
The GT3 project's public Android guide recommends Snapdragon 865-class hardware or equivalent. Its platform notes report a Galaxy A15 5G baseline with reduced race performance. Neither is a compatibility guarantee for the user's POCO device. We must profile the actual target CPU/GPU and tune rendering before claiming full-speed GT4 support.

## 2. PS2 CPU recompilation research

### ran-j/PS2Recomp
- URL: https://github.com/ran-j/PS2Recomp
- License: GPL-3.0.
- Scope: experimental static recompiler and runtime for PS2 ELF programs.
- Useful modules described upstream:
  - `ps2xAnalyzer`: ELF/function analysis and configuration.
  - `ps2xRecomp`: R5900 instruction decoding and C++ code generation.
  - `ps2xRuntime`: guest memory, function dispatch, syscalls and hardware stubs.
  - `ps2xIOP`: R3000A IRX execution and HLE service fallbacks.
- Limitations:
  - Upstream describes the project as experimental and lists incomplete/stubbed hardware paths.
  - Its documented build is C++20 and tested mainly with MSVC; some vector paths require SSE4/AVX. That is not a direct Android ARM64 build.
  - GPL-3.0 obligations must be assessed before copying or linking code into this project.
- Use in this project:
  - Study the instruction decoder, function analysis, guest-memory interface and IOP separation.
  - Build our own correctness-first R5900 interpreter/test suite first.
  - Only adopt code after a written license review and Android/ARM64 compatibility test. Do not assume a desktop recompiler can be dropped into Android.

### PS2 hardware documentation
- URL: https://github.com/ninjadynamics/PS2Docs
- Contents include archived Sony manuals for EE overview, EE peripherals, R5900 core and instruction set, VU0/VU1, GS and SPU2, plus practical implementation notes.
- Use the manuals as architecture references. Verify instruction behavior and register semantics against the relevant manual sections and established emulator tests.

## 3. Android / ARM64 emulator references

### ARMSX2
- URL: https://github.com/ARMSX2/ARMSX2
- Scope: open-source PS2 emulator work for ARM64 platforms, based on PCSX2.
- Use: study Android/ARM64 platform integration, CPU backend boundaries, and performance strategies.
- Limitation: it is an emulator architecture, not a source-level native GT4 port. Its upstream architecture and licensing must be checked before any code reuse.

## 4. Candidate implementation order for this repository

1. **Validate our disc parser** against documented GT4 disc layouts and synthetic fixtures. Add recursive ISO directory traversal and safe bounds checks before depending on nested game files.
2. **Harden CORE extraction**: validate wrapper flags, exact decompressed size, section overlap/ranges, entry-point coverage, and the two embedded hash fields. Test against more than one legally obtained disc revision when available.
3. **Add ELF and memory-image tests** using synthetic fixtures; never execute arbitrary bytes from an ISO during parser tests.
4. **Implement an EE R5900 interpreter** with unit tests for integer instructions, 128-bit GPR semantics, branch delay slots, exceptions and load/store behavior. Add MMI/FPU/COP0 coverage incrementally.
5. **Add an IOP R3000A subsystem** separately, then model required SIF/RPC, CD/DVD and controller services.
6. **Implement VU/VIF/GIF/DMA/GS and SPU2 compatibility** in dependency order, with deterministic tests and frame/audio captures.
7. **Investigate static recompilation or ARM64 JIT** only after the interpreter provides a trusted reference implementation.
8. **Integrate GT4 engine and Adhoc VM**, then test boot/menu/race/save behavior on actual Android hardware.

## 5. Legal and engineering guardrails

- The app must not bundle a GT4 ISO, game executable, GT4 assets, or PS2 BIOS.
- The user supplies their own legally obtained disc image and any required firmware.
- Public source-code licenses are not interchangeable. MIT, GPL-3.0 and unlicensed documentation require different handling.
- A high decompilation percentage does not mean a game is playable: CPU semantics, OS services, I/O, graphics, sound, timing and game data integration remain necessary.
- A successful APK build proves compilation and packaging only. Playability must be demonstrated on the target phone with a real supported game image.

## 6. Current project decision

For this repository, the safest immediate acceleration is to **reuse research, specifications, and test ideas first**, not import a large unrelated emulator or recompiler wholesale. The GT4 decompilation is the strongest game-specific reference; PS2Recomp is a useful CPU/recompiler design reference; PS2Docs is the hardware reference shelf; ARMSX2 is an Android/ARM64 integration reference.


## 7. GT4 RoFS volume format (2026-11-08)

Primary format reference: https://nenkai.github.io/gt-modding-hub/formats/volume/ps2_gt4_volume/

- Retail GT4 uses a RoFS v3.1 volume, not the GT3 archive layout. The apparent GT3-like TOC at the beginning is a decoy.
- For retail GT4, the real TOC header is located at page `0x2159`, with `0x800` bytes per page.
- Header fields include magic (`AC B9 90 AD` encrypted or `RoFS` unencrypted), version 3.1, compressed TOC length, total page count, page length, and TOC page count.
- The page-offset array follows the 0x40-byte header. Encrypted offsets are decoded using the documented key `0x14AC327A`; TOC page payloads use byte XOR key `0x55` before decompression.
- Each page has a reverse TOC at its end. Pages are either index pages or entry pages. Entry node types are directory (0), file (1), or compressed file (2).
- Current implementation validates the retail header and bounded page-offset table, then inflates each TOC page, applies the 0x55 XOR transform for encrypted volumes, and checks page headers plus reverse-TOC entry bounds and node metadata type. It does **not** yet traverse the B-tree, reconstruct directory paths, resolve file nodes, or read/decompress asset payloads. Do not report the archive as extracted or fully parsed.
- The fixed retail TOC page is not assumed to apply to Tourist Trophy, demos, online builds, or every regional/revision variant; those require separate signatures/profile handling.


### GT4FS reference cross-check

The public GT4FS source was reviewed for the on-disk details that are easy to misread:
- TOC page-offset count is `EntryCount + 1`, not `PageCount + 1`.
- Encrypted offsets are XORed with the little-endian bytes of `OffsetCryptKey * (index + 1)`.
- Each TOC page is raw-DEFLATE decompressed; encrypted page bytes are then XORed with `0x55`.
- Record pages and index pages share a 0x0C-byte header and a reverse table of 8-byte entries, but their entry payloads differ.
- The current C++ implementation independently follows these documented structures with explicit read-size, page-count, and range limits. It does not copy GT4FS implementation code.
