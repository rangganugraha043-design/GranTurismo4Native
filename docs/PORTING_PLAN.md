# GT4 Native Android porting plan

## Product goal

Build an Android ARM64 native reimplementation of Gran Turismo 4, with a GT3 Revisited-style app experience:
- the user selects their own legally obtained GT4 disc image;
- the Android app uses a native C++ game engine and Android platform services;
- touch controls and physical controllers are supported;
- graphics and audio are rendered/mixed natively on Android;
- no game ISO, BIOS, or proprietary game assets are bundled in the APK.

A native port is not the same as running the original PS2 executable unchanged. The goal is to reconstruct/adapt GT4's game code and systems for a modern host, not to ship a PS2 emulator disguised as a GT4 app.

## Strategy decision: source-level native port first

The most relevant architecture reference is GT3 Revisited (GT3R). GT3R is a native reimplementation of the GT3 engine: it reads the user's disc data and implements game behavior on modern platforms rather than executing the PS2 game executable in a PS2 emulator. Its Android platform uses SDL3, the Android NDK/CMake, Storage Access Framework, native input/audio/rendering, and an ARM64 application.

GT4 is not GT3. GT3 engine code, file formats, asset assumptions, and game-specific behavior must not be copied as if they were interchangeable. GT3R is a platform/architecture reference only.

For GT4 game logic, the most relevant research base is the matching GT4 decompilation project. The separate gt4-native experiment demonstrates a source-level native-port direction and useful guest-memory/ABI compatibility techniques, but it is early-stage and has no playable build. Its current repository is GPL-3.0; this project will not copy or link its GPL code unless the project deliberately adopts a compatible licensing strategy. Publicly documented concepts and independently implemented compatible interfaces may be studied.

PS2Recomp and PS2 hardware documentation remain useful references for understanding machine behavior and differential testing. A general PS2 CPU/emulator implementation is not the primary product architecture.

## Technical layers

1. **Disc and content layer**
   - Keep the current scanner read-only.
   - Add robust ISO-9660/UDF traversal, nested directories, multi-extent files, and GT4.VOL indexing.
   - Resolve supported GT4 disc revisions and locate required game data.
   - Validate file ranges and hashes before parsing; never execute untrusted data.
   - Use Android SAF URIs/file descriptors so the user can select their own image without broad storage permissions.

2. **GT4 source/runtime compatibility**
   - Pin the matching GT4 decompilation revision and preserve upstream provenance and notices.
   - Build an inventory of source files, symbols, global data, constructors, and unresolved platform dependencies.
   - Define explicit host-side representations for guest addresses, pointers, object layouts, arrays, strings, and callbacks.
   - Port small, independently testable GT4 routines first; compare outputs against known reference behavior.
   - Avoid bulk-importing thousands of source files without a reviewed ABI and licensing path.

3. **Native game systems**
   - Reconstruct game-specific startup, resource management, scene state, vehicle data, race flow, and UI.
   - Replace PS2 OS services with Android/native equivalents where feasible.
   - Recreate the graphics command boundary and implement a Vulkan renderer; validate captured/reference output incrementally.
   - Recreate audio mixing/streaming and controller, memory-card/save, timing, and threading services.
   - Use PS2 manuals and emulator/recompiler projects as behavioral references, not as the shipped runtime.

4. **Android application shell**
   - The app now has a separate SDL3/C++ runtime test activity and native render loop. Next: move SAF URI selection and handoff into the SDL3 host, then replace the test renderer with GT4-specific rendering systems.
   - Keep the Android Java/Kotlin layer small: activity lifecycle, SAF picker, URI/file descriptor handoff, permissions, and Android-specific integration.
   - Native layer owns the game loop, rendering, audio, input mapping, and in-game menus.
   - Support ARM64 first; add other ABIs only when build and performance evidence justify them.
   - Provide touch controls and Bluetooth/USB controller support.

5. **Performance and release**
   - Profile on the actual target phone; RAM capacity alone cannot establish compatibility.
   - Tune resolution, Vulkan pipeline, memory budgets, frame pacing, and thermal behavior.
   - Publish APKs with accurate milestone labels. An APK that opens or scans an ISO is not a playable game.
   - Do not claim the target POCO phone is compatible until the user tests a build on that device.

## Milestones and acceptance criteria

| Milestone | Deliverable | Acceptance evidence |
|---|---|---|
| A — disc inspection | Safe GT4 image identification and content index | Synthetic filesystem tests plus user-image scan report |
| B — Android native shell | SDL3-based Android lifecycle, SAF handoff, native render loop | Reproducible ARM64 APK and CI build |
| C — GT4 runtime foundation | Guest memory/ABI layer and selected native GT4 routines | Automated unit tests and reference comparisons |
| D — asset pipeline | GT4.VOL reader and typed asset decoders | Validated indexes and fixture tests; no bundled game assets |
| E — first native scene | One GT4 scene rendered by the native engine | Repeatable screenshot/frame comparison |
| F — playable slice | Menu, vehicle selection, one track and race loop | Controls, graphics, sound, and stable frame pacing |
| G — broader game | Additional modes, tracks, saves, and polish | Per-feature regression results on Android hardware |

No calendar date is promised for a playable build. The full native port is a substantial reverse-engineering and engine-reconstruction project.

## Current repository status

The existing Android project is an early Kotlin/JNI prototype. It can select a user-provided image and inspect a limited ISO-9660 root directory. It recognizes SYSTEM.CNF, a possible BOOT2 ELF, and root-level CORE.GT4 / GT4.VOL; it can perform bounded raw-DEFLATE and CORE section-table checks.

The repository now includes a minimal SDL3 Android activity and native render loop as a platform smoke test. It does not yet provide GT3R-level SAF integration in the SDL activity, render GT4 assets, or run GT4. The scanner currently lacks recursive directory traversal, full UDF support, multi-extent support, GT4.VOL parsing, the native game loop, renderer, audio engine, and GT4 runtime.

## Current prototype work (2026-11-07)

- Android Storage Access Framework file selection.
- ISO-9660 primary volume descriptor and root directory inspection.
- Read SYSTEM.CNF and report the BOOT2 executable name.
- Identify a standard little-endian ELF32/MIPS bootstrap header when present.
- Locate root-level CORE.GT4 and GT4.VOL entries.
- Inflate the retail CORE.GT4 raw-DEFLATE payload with Android zlib.
- Perform bounded structural checks on the decompressed CORE header, section table, section ranges, and entry-point coverage.
- The scanner is read-only. It does not verify embedded hashes, execute game logic, render graphics, or produce sound.

## Legal and provenance rules

- The APK must not include a copyrighted GT4 ISO, game assets, BIOS, or proprietary SDK files.
- User-supplied game data is read at runtime.
- Keep upstream MIT notices for GT4 decompilation material where applicable.
- Do not copy GPL-3.0 implementation code from experimental projects into this repository without an explicit licensing decision.
- Keep a source/provenance manifest for imported or adapted code.


## SDL3 Android shell work (2026-11-08)

- Pinned SDL3 to release-3.4.12 for both Android Java glue and native CMake sources.
- Added a separate SDLActivity host so the existing Kotlin ISO inspector remains available.
- Added a minimal SDL callback lifecycle, SDL renderer loop, touch-event feedback, and controller connection logging.
- Added CI checkout of SDL3 before Gradle config/build; the SDL source checkout is ignored and is not committed into this repository.
- This is a platform smoke test only. It does not yet implement GT4 gameplay, GT4 rendering, audio playback, SAF handoff into SDL, or a complete controller mapping.
