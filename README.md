# The Simpsons: Hit & Run - Apple TV Port

## Overview

This is a port of *The Simpsons: Hit & Run* to tvOS (Apple TV), built upon the [ZenoArrows](https://github.com/ZenoArrows) repository. A macOS (Apple Silicon) build is also included — it is derived from the Apple TV port and shares the same source code.

**Note:** Game assets are not included. Please place your own game data into the `/assets` folder to build.

---

## 🛠 Requirements

* macOS with **Xcode** installed (Xcode 16 or newer)
* **Premake 5**:

```bash
brew install premake
```

* Your game data copied into the `/assets` folder at the root of the repo. It should contain `art/`, `scripts/`, `movies/`, `sound/` and the `.rcf` files (`ambience.rcf`, `carsound.rcf`, `dialog.rcf`, `music00.rcf` … `soundfx.rcf`).

There are two build scripts in the root, one per platform. Everything they reference is a relative path, so a fresh clone builds anywhere on any machine.

| Script | Target |
| --- | --- |
| `premake5-tvos.lua` | Apple TV (tvOS 14.0+, arm64) |
| `premake5-mac.lua` | Mac (macOS 14.0+, Apple Silicon / arm64) |

---

## 📺 Build for Apple TV

Run both commands from the root of the repo.

**1. Generate the Xcode project:**

```bash
premake5 --file=premake5-tvos.lua xcode4
```

**2. Build it:**

```bash
xcodebuild -project build/tvos/SRR2.xcodeproj -target SRR2 \
  -configuration Debug -sdk appletvos -arch arm64 \
  CODE_SIGNING_ALLOWED=NO
```

The app is written to **`build/tvos/Debug/SRR2.app`**, with the game data and the FFmpeg frameworks already bundled inside it.

`CODE_SIGNING_ALLOWED=NO` produces an unsigned build, which is what you want for packaging into an IPA. **To install straight onto an Apple TV** you need to sign with your own Apple developer account, so open the project in Xcode instead:

```bash
open build/tvos/SRR2.xcworkspace
```

Then pick your team under *Signing & Capabilities*, choose your Apple TV as the run destination, and hit Run.

Swap `-configuration Debug` for `-configuration Release` for an optimized build (output goes to `build/tvos/Release/`).

### Minimum tvOS version

The port targets **tvOS 14.0**, which is the lowest version currently possible. It isn't an arbitrary choice: every prebuilt library in `third_party/` (SDL2, OpenAL Soft, libpng, and all five FFmpeg frameworks) is compiled for `platform TVOS, minos 14.0`. Supporting an older Apple TV means rebuilding all of those dependencies with a lower `-mtvos-version-min` first, then lowering `TVOS_DEPLOYMENT_TARGET` in `premake5-tvos.lua`.

---

## 💻 Build for Mac (ARM64 / Apple Silicon)

Run both commands from the root of the repo.

**1. Generate the Xcode project:**

```bash
premake5 --file=premake5-mac.lua xcode4
```

**2. Build it:**

```bash
xcodebuild -project build/mac/SRR2.xcodeproj -target SRR2 \
  -configuration Debug -sdk macosx -arch arm64 \
  ONLY_ACTIVE_ARCH=YES CODE_SIGN_IDENTITY="-"
```

The app is written to **`build/mac/Debug/SRR2.app`**, ad-hoc signed and ready to run:

```bash
open build/mac/Debug/SRR2.app
```

The Mac build is **controller-only** — connect an Xbox, DualSense or similar controller via the GameController framework.

It links against arm64 dependencies in `MAC_PORT/deps/prefix` (SDL2, OpenAL Soft, libpng, FFmpeg frameworks). These are included in the repo. If that folder is ever missing or you want to rebuild them, run:

```bash
bash MAC_PORT/scripts/build_deps_macos_arm64.sh
```

---

## 📦 Build output summary

| Platform | Generate | Build output | Binary |
| --- | --- | --- | --- |
| Apple TV | `premake5 --file=premake5-tvos.lua xcode4` | `build/tvos/Debug/SRR2.app` | arm64, `platform TVOS`, minos 14.0 |
| Mac ARM64 | `premake5 --file=premake5-mac.lua xcode4` | `build/mac/Debug/SRR2.app` | arm64, `platform MACOS`, minos 14.0 |

The two platforms build into separate folders and never overwrite each other, so you can keep both around at the same time.

### Why two separate Premake files?

Premake's `xcode4` exporter cannot emit one Xcode configuration per platform. If both `tvOS` and `macosx` are declared in a single workspace it collapses them into just `Debug`/`Release` and the last matching filter wins — the result is always a Mac-only project, and building it with `-sdk appletvos` fails on `'OpenGL/gl.h' file not found`. Splitting into one file per platform keeps both generated projects correct. The original combined `premake5.lua` is kept in the root for reference.

---

## 🐛 Progress Tracker

| Feature / Bug | Status |
| --- | --- |
| **Lisa's School Environment** | ✅ FIXED |
| **Camera Controls** | ✅ FIXED |
| **UI & Text Scaling** | ✅ FIXED |
| **NPC Speech Speed** |  ✅ FIXED|
| **General Audio Bugs** | ✅ Mostly FIXED |

---

## 🤝 The Team & Credits

This project has been a massive undertaking, and it wouldn't be where it is today without the dedicated work of **Jveda**.

When I started this, **Jveda** was the only person who stepped up to help. He has been a primary collaborator on this port, specifically:

* **Core Improvements:** He was instrumental in getting the **Camera Controls** and **UI Scaling** to a playable state.
* **The School Grind:** We spent countless hours (and Jveda lost plenty of sleep) debugging the **Lisa’s School environment**. Even when it seemed "unfixable," his dedication to digging through the geometry and textures was what eventually allowed us to get this environment fully fixed and functional. I also added in a mac arm64 port have not tested it that much so it still needs work but it should be stable just need tiny fixes like saves etc  
