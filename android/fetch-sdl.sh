#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDL_DIR="$ROOT/third_party/SDL"
TAG="release-3.4.12"

if [[ ! -f "$SDL_DIR/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java" ]]; then
    rm -rf "$SDL_DIR"
    git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git "$SDL_DIR"
fi

echo "SDL3 $TAG ready: $SDL_DIR"
