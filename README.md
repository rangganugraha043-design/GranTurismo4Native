# GranTurismo4Native

Native Android ARM64 research project for a Gran Turismo 4 native port.

## Current status: prototype only
This repository currently builds an Android shell with:
- Android Storage Access Framework ISO file picker.
- C++ JNI library.
- Read-only ISO-9660 signature and root-directory probe.
- Detection of `SYSTEM.CNF`, its `BOOT2` path, and a basic ELF32/MIPS header.
- GitHub Actions workflow for a debug APK.

**This is not a playable GT4 port yet.** A PS2 disc image contains software for the Emotion Engine (MIPS R5900), IOP (MIPS R3000A), vector units, DMA/VIF/GIF, Graphics Synthesizer and SPU2. Android ARM64 cannot run that software directly. A real port needs a CPU translation/runtime and substantial PS2 subsystem compatibility work.

## Research direction
The project will use public reverse-engineering/decompilation research as engineering references, not redistribute game code or assets. In particular, the GT4 decompilation research documents the `CORE.GT4` executable format and is a starting point for a future extraction/loader phase. This does not mean that a ready-made Android runtime exists.

## Build
Run `gradle assembleDebug` with JDK 17, Android SDK 35, NDK 27.2.12479018 and CMake 3.22.1.

APK output: `app/build/outputs/apk/debug/app-debug.apk`.

## Development plan
See [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) for the staged plan and [docs/RESEARCH_NOTES.md](docs/RESEARCH_NOTES.md) for reviewed external GT4/PS2 research, upstream links, license notes and reuse decisions.

The app does not bundle game images or BIOS files. Use only a disc image you are legally entitled to use.
