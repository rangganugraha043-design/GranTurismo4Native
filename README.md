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

The scanner does not yet support UDF-only images, multi-extent ISO files, or traversing the RoFS B-tree, resolving file paths, and extracting/decompressing file payloads from `GT4.VOL`; it also does not load/execute the GT4 program. The app does not bundle game images or BIOS files. Use only a disc image you are legally entitled to use.


### GT4 RoFS index progress (2026-11-08)

The read-only GT4.VOL inspector now decompresses every v3.1 TOC page, validates record/index page structures and record metadata ranges, reads parent node IDs and directory/file metadata, and resolves paths by following directory node IDs. It reports a bounded sample of discovered file paths and sizes. It still does not read file payload pages, inflate compressed assets, decode textures/models/audio, or start the GT4 runtime. The parser currently targets the retail GT4 TOC location (`0x2159` pages) and is not a universal parser for every GT4-family volume.
