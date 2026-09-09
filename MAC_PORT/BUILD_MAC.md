# Build Mac arm64 (Apple Silicon)

tvOS support remains in the same Premake project. Mac uses rebuilt deps under `MAC_PORT/deps/prefix`.

## One-time deps (USB)

Space-free build tree (FFmpeg cannot handle spaces in paths):

```bash
bash /Volumes/Untitled2/MAC/srr2mac_build/build_all.sh
```

Artifacts sync into:

`MAC_PORT/deps/prefix/` (libSDL2.a, libopenal.a, libpng16.a, FFmpeg frameworks)

## Generate + build

```bash
cd "/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3"
premake5 xcode4
xcodebuild -project build/SRR2.xcodeproj \
  -target SRR2 -configuration Debug \
  -sdk macosx -arch arm64 \
  ONLY_ACTIVE_ARCH=YES \
  CODE_SIGN_IDENTITY="-"
```

App output:

`build/Debug/SRR2.app` (also copied to `MAC_PORT/build/products/SRR2.app`)

## Run

```bash
open "MAC_PORT/build/products/SRR2.app"
```

Controller-only: connect an Xbox / DualSense / etc. via GameController.

## Platforms in Premake

- `platforms { "tvOS", "macosx" }`
- tvOS still uses root `third_party/` (unchanged)
- macosx uses `MAC_PORT/deps/prefix` + AppKit/OpenGL (no UIKit/OpenGLES)
