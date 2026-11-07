# GranTurismo4Native

Native Android ARM64 research project for a Gran Turismo 4 native port.

## Current status: prototype only
This repository currently builds an Android shell with:
- Android Storage Access Framework ISO file picker.
- C++ JNI library.
- Read-only ISO-9660 signature probe.
- GitHub Actions workflow for a debug APK.

**This is not a playable GT4 port.** An ISO file contains PS2/MIPS software and PS2-specific system calls, graphics, audio, DMA and hardware behavior. Android ARM processors cannot execute that image directly. A real native port requires substantial reverse engineering and reimplementation/translation of the game's CPU code and PS2 subsystems. This project intentionally does not bundle game images or BIOS files.

## Build
Run `gradle assembleDebug` with JDK 17, Android SDK 35, NDK 27.2.12479018 and CMake 3.22.1.

APK output: `app/build/outputs/apk/debug/app-debug.apk`.

## Next engineering milestones
1. Validate disc image formats and safely enumerate ISO/UDF filesystems.
2. Identify the legally obtained game executable and document its format.
3. Research MIPS-to-ARM64 translation and PS2 subsystem dependencies.
4. Implement and test subsystems incrementally; no gameplay claim until verified.
