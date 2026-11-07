# Android ARM64 porting plan

## Goal
Build a native Android application for a legally dumped copy of Gran Turismo 4. The app must not contain Sony game data, BIOS images, or copyrighted assets. The user selects their own disc image.

## Important distinction
"Native Android app" does not mean that an unmodified PS2 ISO can execute directly on an ARM phone. GT4 targets the PlayStation 2 Emotion Engine (MIPS R5900 plus VU0/VU1), IOP (MIPS R3000A), DMA/VIF/GIF, Graphics Synthesizer, and SPU2 audio hardware. Those interfaces must be translated or reproduced.

## Research starting points
- `gustavocanepa/gt4-decomp`: matching decompilation work for the GT4 NTSC-U executable. Its README identifies `CORE.GT4` as raw DEFLATE, loaded at `0x00100000`, with entry `0x00100008`. This is research input, not a ready-to-run Android engine.
- `GT4Recomp` / Recompendium project: demonstrates ahead-of-time translation of EE code and VU microprograms, while still relying on PS2 graphics/runtime components. It is not a drop-in Android native port.
- PS2 architecture references: EE and IOP are separate processors; VU0/VU1 and GS behavior are central to correct gameplay and rendering.

## Engineering phases

### Phase 0 — disc inspection (current prototype)
- Android Storage Access Framework file selection.
- Read-only ISO-9660 descriptor and root directory inspection.
- Read `SYSTEM.CNF`, identify `BOOT2`, and inspect the executable ELF header.
- Add tests using synthetic ISO fixtures. Do not write to the selected disc image.

### Phase 1 — game executable extraction
- Support ISO-9660 directory traversal and multi-extent files safely.
- Resolve the boot executable from `SYSTEM.CNF`.
- Detect the GT4 executable variant and verify its expected signature.
- Implement bounded raw-DEFLATE extraction for `CORE.GT4` using Android's system zlib.
- Validate output size, ELF headers, segment ranges, entry point, and hashes. Never execute arbitrary bytes from an untrusted image.

### Phase 2 — CPU and runtime foundation
- Define EE R5900 architectural state, 128-bit GPR behavior, exceptions, branch delay behavior, COP0/COP1 and relevant multimedia instructions.
- Implement an instruction decoder and a correctness-first interpreter with unit tests.
- Add an ARM64 dynamic recompiler only after interpreter correctness is established.
- Treat IOP R3000A and its IRX modules as a separate execution environment.

### Phase 3 — PS2 subsystem compatibility
- EE memory map, scratchpad, timers, interrupts and DMA.
- VIF0/VIF1, GIF packet handling, VU0/VU1 execution and synchronization.
- GS local memory, pixel formats, blending, depth tests, texture sampling and display timing.
- IOP disc, pad, memory-card and RPC services.
- SPU2 audio, streaming and synchronization.
- IPU/MPEG and other media paths used by the game.

### Phase 4 — GT4 game integration
- Load the game executable and its data files from the user's image.
- Recreate required OS/runtime services and game-specific interfaces.
- Validate boot, menus, garage, tracks, replays, licenses and saves separately.
- Add controller mapping, audio settings, frame pacing and crash-safe save handling.

### Phase 5 — Android performance and release
- Profile on the actual target phone; do not infer performance from RAM alone.
- Tune ARM64 code generation, Vulkan rendering, memory use and thermal behavior.
- Test long sessions, suspend/resume, touch/gamepad input and multiple disc revisions.
- Publish debug APKs only with an explicit status label until gameplay tests pass.

## Current limitations
The existing repository is a prototype shell and disc inspector, not a playable port. The current ISO scanner only reads the ISO-9660 primary volume descriptor and root directory; it is not yet a complete UDF/ISO filesystem implementation. No EE/IOP CPU, VU, GS, SPU2 or GT4 game runtime exists yet.

## Acceptance criteria
A milestone is complete only when it has automated tests and a reproducible build. "Playable" means a user can select a supported own disc image, boot into GT4, navigate menus, start and finish a race with working graphics, sound, controls and save/load on the target Android device.
