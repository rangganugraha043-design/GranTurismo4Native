# GranTurismo4Native

Native Android research project for a Gran Turismo 4 native port. The Android app now includes a separate SDL3 runtime test activity; it is not yet a playable GT4 engine.

## Current status: prototype only (November 2026)
This repository currently builds an Android shell with:
- Android Storage Access Framework ISO file picker.
- C++ JNI library for read-only disc inspection.
- SDL3 native Android activity with a basic renderer loop and touch-event feedback test.
- SAF-to-native file-descriptor handoff: the selected ISO is opened by Android and passed to SDL/C++ without copying the entire image.
- Runtime smoke test reads the selected descriptor's size and ISO-9660 primary-volume-descriptor signature.
- Read-only ISO-9660 signature and bounded recursive directory scan (up to 8 levels).
- Detection of nested `SYSTEM.CNF`, its `BOOT2` path, and a basic ELF32/MIPS header.
- Detection and bounded raw-DEFLATE/header inspection of `CORE.GT4`.
- Detection of `GT4.VOL`, bounded validation of the retail RoFS v3.1 header, encrypted page-offset table, and decompressed TOC page structures, and persisted access to the last selected document when Android permits it.
- GitHub Actions workflow for a debug APK.

**The SDL3 screen is only a platform/runtime + ISO-descriptor handoff smoke test. It does not yet parse the full disc from SDL, render GT4 assets, run GT4 logic, or produce game audio. This is not a playable GT4 port yet.** A PS2 disc image contains software for the Emotion Engine (MIPS R5900), IOP (MIPS R3000A), vector units, DMA/VIF/GIF, Graphics Synthesizer and SPU2. Android ARM64 cannot run that software directly. A real port needs a CPU translation/runtime and substantial PS2 subsystem compatibility work.

## Research direction
The project will use public reverse-engineering/decompilation research as engineering references, not redistribute game code or assets. In particular, the GT4 decompilation research documents the `CORE.GT4` executable format and is a starting point for a future extraction/loader phase. This does not mean that a ready-made Android runtime exists.

## Build
Run `bash android/fetch-sdl.sh` first, then `gradle assembleDebug` with JDK 17, Android SDK 35, NDK 27.2.12479018 and CMake 3.22.1. The script fetches SDL 3.4.12; the checkout is ignored by Git.

APK output: `app/build/outputs/apk/debug/app-debug.apk`.

## Development plan
See [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) for the staged plan and [docs/RESEARCH_NOTES.md](docs/RESEARCH_NOTES.md) for reviewed external GT4/PS2 research, upstream links, license notes and reuse decisions.

The scanner still does not support UDF-only images or multi-extent ISO files, and it does not load/execute the GT4 program. The app does not bundle game images or BIOS files. Use only a disc image you are legally entitled to use.


### GT4 RoFS index progress (2026-11-08)

The read-only GT4.VOL inspector now decompresses every v3.1 TOC page, validates record/index page structures and record metadata ranges, reads parent node IDs and directory/file metadata, and resolves paths by following directory node IDs. It reports a bounded sample of discovered file paths and sizes. It can now attempt bounded reads of selected file payloads, including PS2Zip-wrapped raw-DEFLATE data. This is only a smoke test: it does not yet provide a complete extraction pipeline or decode textures/models/audio, and it does not start the GT4 runtime. The parser currently targets the retail GT4 TOC location (`0x2159` pages) and is not a universal parser for every GT4-family volume.


The inspector also performs a bounded sample of file-payload reads using the RoFS data-start/page-offset calculation. For compressed records it attempts raw-DEFLATE expansion and checks the declared output size; this is a format-validation step, not yet a complete asset extraction/decoding pipeline.


The RoFS locator now searches the first 10,000 volume pages for the v3.1 header instead of assuming one fixed TOC page, and validates the big-endian magic plus little-endian version/header fields. File-payload sampling recognizes the PS2Zip 8-byte wrapper (`C5 EE F7 FF`, negative expanded length) before inflating its raw-DEFLATE body. The root directory marker is normalized so child paths do not receive a spurious `./` prefix.


### SDL runtime now calls the RoFS reader

The SDL3 native activity now links against the same `gt4native` library used by the launcher. When the selected ISO contains `GT4.VOL`, the runtime passes the volume's file-descriptor range to the shared read-only RoFS inspector and writes the detailed TOC/path/payload-sample report to Android logcat. This removes the previous gap where only the launcher inspected RoFS while the SDL screen merely detected the volume. The runtime still does not load GT4 code, decode game assets, or render gameplay.


### Current build verification

The latest GitHub Actions run for commit `96b28337ed5833066bbd14da9f5a433ca410b122` completed successfully. This verifies that the Android debug APK compiles; it does not verify parsing against a real GT4 image or gameplay on a phone. The next validation gate is to test the reader against a legally owned GT4 image and record the exact TOC/path/payload results before beginning asset decoders.
