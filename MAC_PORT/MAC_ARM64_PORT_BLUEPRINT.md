# Mac ARM64 (Apple Silicon / M4) Port Blueprint

**Status:** AUDIT ONLY — no code or build changes in this pass  
**Date:** 2026-08-12  
**Target:** macOS arm64 (Apple Silicon M4), **controller-only** input  
**Source baseline:** Working tvOS arm64 port in this repo (`SRR2.app`, already arm64)  
**Constraint:** Everything for the Mac port stays on this USB under `MAC_PORT/` (and project paths under this volume). Do not rely on rebuilding into `/usr/local` or overwriting the existing tvOS `third_party/` trees.

---

## 0. Executive verdict

You already solved the hard game-side work on **tvOS arm64** (GLES2 render path, GameController input, OpenAL streaming, FFmpeg FMV, asset VFS, Premake/Xcode monorepo).

What is **not** done for Mac:

1. Every prebuilt in `third_party/` is **arm64 + tvOS platform**, not macOS. Linking them into a Mac app will fail.
2. The app is hard-wired to **tvOS SDK + UIKit + OpenGLES + `SDL_UIKitRunApp`**.
3. Premake only generates a **tvOS** Xcode project (`SDKROOT=appletvos`, `TARGETED_DEVICE_FAMILY=3`).
4. There is **no Mac platform target** yet. `MAC_PORT/` was empty before this blueprint.

**Bottom line:** This is a **platform + dependency rebuild port**, not a from-scratch game rewrite. Controller input can largely reuse the existing GameController stack. Graphics needs a Mac GLES strategy (ANGLE recommended). Expect rebuild work on SDL2, OpenAL Soft, libpng, and FFmpeg before the game binary will even link.

---

## 1. Current project inventory (what exists today)

### 1.1 Layout

| Path | Role | Notes |
|------|------|-------|
| `premake5.lua` | Only build generator | Platforms = `{ "tvOS" }` only |
| `Info.plist` | tvOS-style app plist | No Mac keys / no GC entitlement hints |
| `src/code/main/tvosmain.mm` | Entry | Calls `SDL_UIKitRunApp` |
| `src/code/main/tvosplatform.*` | Platform bootstrap | GLES2 ES profile window, controller wait |
| `src/code/input/tvos/*` | Native pad backend | GameController.framework |
| `src/libs/radcore/.../tvoscontroller.cpp` | RadController bridge | Behind `#ifdef RAD_TVOS` |
| `src/libs/radcore/.../radfile/tvos/*` | Asset / save VFS | `Assets/TheSimpsons` + Caches prefs |
| `src/libs/pure3d/pddi/gles/**` | Renderer | GLES2; headers from OpenGLES.framework on tvOS |
| `src/libs/pure3d/pddi/gles/glad/**` | GLES2 loader | Present but **excluded** from tvOS build |
| `third_party/SDL2` | Static SDL2 | arm64, **platform TVOS**, UIKit video driver |
| `third_party/OpenALSoft` | Static OpenAL | arm64, **platform TVOS** |
| `third_party/libpng` | Static png16 | arm64, **platform TVOS** |
| `third_party/ffmpeg/*.framework` | Dyn frameworks | arm64, **platform TVOS**, minos 14.0 |
| `build/Debug/SRR2.app` | Last tvOS build | Mach-O arm64; links UIKit + OpenGLES |
| `assets/` | Game data (~1.8 GB) | Already present; post-build copies into app |
| `MAC_PORT/` | Mac port workspace | Blueprint lives here; keep all Mac artifacts here |

### 1.2 Host machine (audit-time)

- Host CPU: `arm64` (Apple Silicon)
- macOS: 15.7.x
- Xcode SDKs present: `MacOSX15.5.sdk`, `AppleTVOS18.5.sdk`
- Tools on PATH: `premake5` (5.0.0-beta8), `cmake`, `pkg-config`
- USB volume free space: ~287 GB (enough for deps + build trees)

### 1.3 Confirmed: third_party is tvOS, not Mac

`vtool` / `otool` on all linked deps show:

```
architecture: arm64
platform: TVOS   (Mach-O platform id 3)
minos: 14.0
sdk: 18.5
```

Checked binaries:

- `third_party/SDL2/lib/libSDL2.a`
- `third_party/OpenALSoft/lib/libopenal.a`
- `third_party/libpng/lib/libpng16.a`
- `third_party/ffmpeg/libavcodec.framework/libavcodec`
- `third_party/ffmpeg/libavformat.framework/libavformat`
- `third_party/ffmpeg/libavutil.framework/libavutil`
- `third_party/ffmpeg/libswresample.framework/libswresample`
- `third_party/ffmpeg/libswscale.framework/libswscale`

SDL config also proves UIKit build:

- `SDL_VIDEO_DRIVER_UIKIT 1`
- Cocoa driver **undefined**

**Rule for this port:** never overwrite these trees. Install Mac rebuilds under:

```
MAC_PORT/deps/
  SDL2/
  OpenALSoft/
  libpng/
  ffmpeg/
MAC_PORT/build/          # generated Xcode / objects / .app
MAC_PORT/scripts/        # rebuild scripts (later implementation phase)
MAC_PORT/notes/          # optional working notes
```

Keep root `third_party/` as the **tvOS** known-good set.

---

## 2. Goal definition (scope lock)

### In scope

- Native **macOS arm64** `.app` that runs on M4 Mac
- **Controller-only** play (Xbox / DualSense / etc. via GameController)
- Same game code / assets as tvOS port
- Recompile / replace frameworks and static libs for **macosx**
- Keep all Mac work products on this USB (`MAC_PORT/`)

### Out of scope (for first working Mac build)

- Keyboard / mouse primary control (unless needed for debug menus)
- Shipping App Store packaging / notarization polish (can be phase 2)
- Fixing existing tvOS audio/NPC bugs listed in `README.md` / `research.txt`
- Intel (x86_64) Mac support
- Metal rewrite of the renderer (unless ANGLE path fails)

### Success criteria (MVP)

1. App launches on macOS arm64 without tvOS SDK.
2. Window opens; GLES2 (or ANGLE GLES) presents frames.
3. GameController pad works through existing tvOS controller bridge (adapted).
4. Assets load from Mac app bundle path.
5. Audio (OpenAL Soft) + FMV (FFmpeg) play.
6. Save data writes under a writable Mac path.

---

## 3. Architecture map: tvOS → Mac

```
┌─────────────────────────────────────────────────────────────┐
│                         GAME CORE                           │
│  (mission / world / sound logic / scrooby / pure3d GLES)    │
│  Mostly reusable; gated by RAD_TVOS / RAD_CONSOLE defines   │
└───────────────┬───────────────────────────────┬─────────────┘
                │                               │
     ┌──────────▼──────────┐         ┌──────────▼──────────┐
     │  INPUT (keep)       │         │  RENDER (adapt)     │
     │  GameController.mm  │         │  pddi/gles + SDL GL │
     │  tvoscontroller.cpp │         │  Needs Mac GLES path│
     └──────────┬──────────┘         └──────────┬──────────┘
                │                               │
     ┌──────────▼──────────┐         ┌──────────▼──────────┐
     │  PLATFORM SHELL     │         │  DEPS (rebuild)     │
     │  tvosmain / platform│         │  SDL2 Cocoa+ANGLE?  │
     │  UIKit → Cocoa/SDL  │         │  OpenAL / png / FF  │
     └─────────────────────┘         └─────────────────────┘
```

### What can stay conceptually the same

- Monolithic Premake project compiling game + libs into one app
- `RAD_CONSOLE` + GLES2 renderer design
- GameController-based controller mapping (already bypasses SDL pads)
- OpenAL Soft HAL under `radsound/.../win32` (already used on tvOS)
- FFmpeg movie player path
- Asset layout `Assets/TheSimpsons/...`

### What must change

| Layer | tvOS today | Mac requirement |
|-------|------------|-----------------|
| SDK | `appletvos` | `macosx` |
| Entry | `SDL_UIKitRunApp` | Cocoa / `SDL_main` (no UIKit runner) |
| Video driver | SDL UIKit | SDL Cocoa (+ ANGLE if keeping GLES) |
| GL API headers | `OpenGLES.framework` | No OpenGLES on macOS → ANGLE **or** glad GLES via ANGLE **or** desktop GL |
| UI frameworks | UIKit | AppKit (pulled by SDL Cocoa); drop UIKit link |
| Bundle | Flat `.app` (tvOS) | Standard Mac `.app` (`Contents/MacOS`, `Contents/Frameworks`, `Contents/Resources`) |
| Prefs path | `~/Library/Caches/Radical/Simpsons` (tvOS choice) | Prefer `Application Support` on Mac |
| Premake platform | `tvOS` only | Add `macosx` / `Mac` platform filter |
| Third-party bins | tvOS arm64 | macOS arm64 rebuilds in `MAC_PORT/deps` |

---

## 4. Critical blockers (ordered)

### BLOCKER A — Prebuilt deps are wrong platform

Even though architecture is already arm64, Mach-O **platform tag is TVOS**. Xcode will refuse (or produce an invalid binary) if you link these into a Mac target.

**Action:** Rebuild all of:

1. SDL2 (static or shared) for `macosx` / `arm64`
2. OpenAL Soft static for `macosx` / `arm64`
3. libpng16 static for `macosx` / `arm64`
4. FFmpeg frameworks (or dylibs) for `macosx` / `arm64`:
   - libavcodec, libavformat, libavutil, libswresample, libswscale
   - (libavdevice / libavfilter exist in tree but are not linked by Premake today — leave optional)

Install outputs under `MAC_PORT/deps/...` only.

### BLOCKER B — No OpenGLES.framework on macOS

`src/libs/pure3d/pddi/gles/gl.hpp` currently:

```cpp
#elif defined(RAD_TVOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <glad/glad.h>
#endif
```

Mac cannot use the RAD_TVOS OpenGLES include path as-is.

**Recommended strategy (preserve most renderer code):**

1. Build SDL2 for Mac with **ANGLE** (EGL + GLES2 over Metal), **or** vendor ANGLE separately and create an ES2 context.
2. For Mac build, either:
   - Treat Mac like “Apple GLES port” and include GLES headers from ANGLE, **or**
   - Enable the existing `glad/` GLES2 loader (already in tree, currently excluded by Premake for tvOS).

**Fallback strategy (more code churn):**

- Desktop OpenGL Core/Compat via SDL Cocoa + glad desktop profile  
- Higher risk of shader / extension / precision differences vs current GLES2 shaders

**Do not** plan on linking `OpenGLES.framework` on Mac — it is not a Mac system framework.

### BLOCKER C — Entry / shell is UIKit-specific

`tvosmain.mm`:

```cpp
return SDL_UIKitRunApp( argc, argv, SDL_main );
```

That symbol exists only in UIKit SDL builds. Mac needs a Cocoa entry path.

Also `tvos_controller.mm` imports UIKit only to log `UIApplication` state — not required for pad I/O. GameController itself **is** available on macOS.

### BLOCKER D — Premake is tvOS-only

Workspace settings force:

- `SDKROOT = appletvos`
- `platforms { "tvOS" }`
- `TARGETED_DEVICE_FAMILY = 3`
- `TVOS_DEPLOYMENT_TARGET = 14.0`
- Links: `UIKit.framework`, `OpenGLES.framework`
- Defines: `RAD_TVOS`, `RAD_GLES`, etc.

Need a second platform (or a Mac-only Premake fork under `MAC_PORT/`) that targets macosx and points libdirs at `MAC_PORT/deps`.

### BLOCKER E — Define gate assumes RAD_TVOS

`raddebug.hpp` errors unless one of: `RAD_GAMECUBE`, `RAD_PS2`, `RAD_XBOX`, `RAD_WIN32`, `RAD_TVOS`.

~138 source files reference `RAD_TVOS`.

Two viable policies (pick one before coding):

| Policy | Pros | Cons |
|--------|------|------|
| **A. Keep `RAD_TVOS` on Mac** as “Apple console GLES port” alias, add `RAD_MACOS` only where behavior diverges | Fastest MVP; minimal `#ifdef` churn | Name is misleading; must carefully split UIKit-only bits |
| **B. Introduce `RAD_MACOS` properly** and extend every gate | Clean long-term | Large mechanical edit surface |

**Recommendation for MVP:** Policy A + small surgical `RAD_MACOS` / `TARGET_OS_OSX` splits for UIKit, OpenGLES headers, entrypoint, prefs path, window flags.

---

## 5. Framework / linkage matrix

### 5.1 Current tvOS link set (from Premake + `otool -L` on built app)

| Item | tvOS | Mac MVP plan |
|------|------|--------------|
| `GameController.framework` | weak + strong | **Keep** (controller-only path) |
| `CoreHaptics.framework` | weak | Keep weak (optional rumble later) |
| `Foundation` / `CoreFoundation` | yes | Keep |
| `CoreVideo` | yes | Keep |
| `CoreAudio` / `AudioToolbox` | yes | Keep |
| `AVFoundation` | yes | Keep (movie / media) |
| `CoreBluetooth` | yes | Keep if SDL/GC needs it; verify after SDL rebuild |
| `CoreGraphics` | yes | Keep |
| `Metal` | yes | Keep (ANGLE / future) |
| `QuartzCore` | yes | Keep |
| `OpenGLES.framework` | yes | **Remove** → replace with ANGLE GLES **or** OpenGL.framework if desktop-GL path |
| `UIKit.framework` | yes | **Remove** → AppKit via SDL Cocoa |
| `libz` | yes | Keep (system) |
| `libc++` / `libSystem` | yes | Keep |
| Static `libSDL2.a` | tvOS build | **Rebuild** Mac Cocoa (+ANGLE) |
| Static `libopenal.a` | tvOS build | **Rebuild** Mac |
| Static `libpng16.a` | tvOS build | **Rebuild** Mac |
| FFmpeg `@rpath/*.framework` | tvOS build | **Rebuild** Mac frameworks; embed in `Contents/Frameworks` |

### 5.2 Likely **new** Mac link needs (depends on SDL/ANGLE choice)

- `AppKit.framework` (usually pulled by SDL)
- `IOKit.framework` (SDL / controllers / HID)
- `ForceFeedback.framework` (sometimes SDL; optional)
- `Carbon.framework` / `CoreServices` (legacy SDL bits; verify)
- `OpenGL.framework` **only if** not using ANGLE
- ANGLE libs / dylibs / xcframework if chosen (`libEGL`, `libGLESv2`, etc.)
- `libiconv` if FFmpeg needs it
- Possibly `bz2`, `lzma` depending on FFmpeg configure

Exact list must be regenerated after each dep rebuild (`otool -L`).

---

## 6. Source-level port surface (files that will need Mac work)

> This section is an audit checklist for a future implementation pass. **No edits in this audit.**

### 6.1 Must adapt (platform shell)

| File | Why |
|------|-----|
| `src/code/main/tvosmain.mm` | Replace `SDL_UIKitRunApp`; Mac entry |
| `src/code/main/tvosplatform.cpp` / `.h` | Window flags (fullscreen TV → resizable/windowed Mac), GL profile attrs, controller wait UX |
| `Info.plist` (or `MAC_PORT/Info-mac.plist`) | Mac bundle keys; drop TV-only keys; add high-res / GC usage descriptions if needed |
| `premake5.lua` **or** `MAC_PORT/premake5_macos.lua` | New macosx platform, frameworks, libdirs → `MAC_PORT/deps` |

### 6.2 Must adapt (graphics)

| File | Why |
|------|-----|
| `src/libs/pure3d/pddi/gles/gl.hpp` | OpenGLES includes invalid on Mac |
| `src/libs/pure3d/pddi/gles/display_tvos/gldisplay.cpp` | Mostly SDL-GL already — good; verify ES extensions (`GL_OES_depth24`, VAO OES) under ANGLE |
| `src/libs/pure3d/pddi/gles/glcon.cpp`, `gltex.cpp`, `glmat.cpp`, `glprog.cpp` | Extension / precision assumptions; diag code OK |
| Premake GLES file list | Possibly re-include `glad/**` for Mac |

### 6.3 Must adapt (input) — smaller than it looks

| File | Why |
|------|-----|
| `src/code/input/tvos/tvos_controller.mm` | Drop UIKit `UIApplication` logging; keep GCController |
| `src/libs/radcore/src/radcontroller/tvoscontroller.cpp` | Keep behind shared Apple define |
| `src/code/input/inputmanager.cpp` | RAD_TVOS branches — verify Mac still hits console mapping path |
| `src/code/main/tvosplatform.cpp` `HasAnyController` / wait loop | Should call native TvOS input API on Mac too |

**Controller-only note:** Existing design already avoids SDL joystick subsystems on purpose (“SDL2's broken controller support on tvOS”). That same design is the right Mac MVP: **GameController.framework only**.

### 6.4 Must adapt (filesystem / saves)

| File | Why |
|------|-----|
| `src/libs/radcore/src/radfile/tvos/tvosdrive.cpp` | Pref root currently forces `Library/Caches/...` for tvOS; Mac should use Application Support (legacy migration helpers already exist in this file) |
| Asset post-build copy rules | Mac bundle layout differs from tvOS flat bundle |
| `memorycardmanager.cpp` | RAD_TVOS save behavior — confirm paths |

### 6.5 Likely compile-only / ifdef carry-over

Hundreds of `RAD_TVOS` diag / audio / render branches can stay if Policy A keeps `RAD_TVOS` defined on Mac. Still scan for:

- UIKit types
- OpenGLES-only symbols not in ANGLE
- tvOS-only entitlements / Top Shelf / sandbox assumptions
- Hardcoded 1920x1080 fullscreen assumptions vs Retina drawable sizes (SDL already queries drawable size — verify Mac retina)

### 6.6 Explicitly do **not** need for controller-only MVP

- Win32 keyboard/mouse input files currently excluded
- `sdlcontroller.cpp` (excluded today; keep excluded if using native GC)
- Desktop `pddi/gl/**` (non-GLES) unless abandoning GLES strategy

---

## 7. Dependency rebuild plan (all on USB)

### 7.1 Directory contract

```
MAC_PORT/
  MAC_ARM64_PORT_BLUEPRINT.md          ← this file
  deps/
    prefix/                            ← common install prefix (recommended)
      include/
      lib/
      Frameworks/                      ← FFmpeg + ANGLE if used
    src/                               ← optional: downloaded sources (stay on USB)
  build/
    xcode/                             ← generated Mac Xcode project
    products/                          ← SRR2.app output
  scripts/                             ← future rebuild scripts
```

Use a single prefix, e.g.:

`MAC_PORT/deps/prefix`

Then Premake Mac libdirs / include dirs / embed paths all point here.

### 7.2 SDL2 (highest priority)

**Why first:** entrypoint, window, GL/GLES context, event pump.

Current tree is UIKit/tvOS. Need Mac build with:

- Video: **Cocoa**
- Render/context: **OpenGL ES via ANGLE** (recommended) **or** desktop GL
- Joystick/GameController: can enable for debugging, but game should still use native GC backend
- Static lib preferred to match current Premake (`links { "SDL2" }`)

Verify after build:

```bash
vtool -show-build MAC_PORT/deps/prefix/lib/libSDL2.a
# expect: platform MACOS, arm64
grep SDL_VIDEO_DRIVER_ MAC_PORT/deps/prefix/include/SDL2/SDL_config.h
# expect: COCOA 1, not UIKIT
```

### 7.3 OpenAL Soft

Rebuild static `libopenal.a` for macosx arm64 into `MAC_PORT/deps/prefix`.

Keep `AL_LIBTYPE_STATIC` define in Premake (already present).

### 7.4 libpng

Rebuild `libpng16.a` (+ zlib from system is fine) for macosx arm64.

### 7.5 FFmpeg frameworks

Current packaging is Apple-style `.framework` bundles embedded via rsync + codesign in postbuild.

For Mac:

- Rebuild same five libs for macosx arm64
- Prefer keeping `.framework` layout to minimize Premake postbuild churn **or** switch to dylibs + `@rpath` (document choice when implementing)
- Configure with VideoToolbox allowed on Mac (good for M4 decode), disable unavailable tvOS-only bits
- Embed into `Contents/Frameworks`
- Set `LD_RUNPATH_SEARCH_PATHS = @executable_path/../Frameworks` (Mac) instead of `@executable_path/Frameworks` (tvOS flat)

### 7.6 ANGLE (if GLES-on-Metal path chosen)

Options:

1. Build ANGLE as dylibs/frameworks into `MAC_PORT/deps/prefix`
2. Or use SDL2’s ANGLE integration if building SDL with ANGLE support

Deliverables needed by the game:

- EGL + GLESv2 headers
- Runtime libs loadable by SDL / the app

### 7.7 What not to do

- Do not `brew install` and leave binaries only on the internal disk without copying into `MAC_PORT/deps` (USB-portability requirement).
- Do not replace root `third_party/*` tvOS artifacts.
- Do not mix tvOS and macOS objects in one `.a`.

---

## 8. Premake / Xcode blueprint (future edits)

Create either:

- `MAC_PORT/premake5_macos.lua` that includes shared lists from a refactored root script, **or**
- extend root `premake5.lua` with `platforms { "tvOS", "macosx" }` and hard filters.

### Required Mac workspace settings

- `SDKROOT = macosx`
- `MACOSX_DEPLOYMENT_TARGET` (suggest 14.0 or 13.0 — decide at implement time)
- `ARCHS = arm64` / Premake `architecture "ARM64"`
- `PRODUCT_BUNDLE_IDENTIFIER` e.g. `com.simpsonsmac.srr2`
- `INFOPLIST_FILE` → Mac plist under `MAC_PORT/`
- `LD_RUNPATH_SEARCH_PATHS = @executable_path/../Frameworks`
- Remove `TARGETED_DEVICE_FAMILY = 3`
- Remove `-fPIE` tvOS requirement if inappropriate (verify; PIE is fine on Mac too)

### Required Mac defines (proposed)

```
RAD_RELEASE (or Debug equivalent)
RAD_CONSOLE
RAD_TVOS              # Policy A alias for shared Apple GLES port code
RAD_MACOS             # new: Mac-specific branches
RAD_GLES
RAD_GLES_VERSION=2
AL_LIBTYPE_STATIC
(+ existing diag macros as desired)
```

### Required Mac links (starting set)

- Deps: `SDL2`, `png16`, `openal`, `z`
- System: GameController, Foundation, CoreFoundation, CoreVideo, CoreAudio, AudioToolbox, AVFoundation, CoreGraphics, Metal, QuartzCore, AppKit, IOKit
- Weak: CoreHaptics, GameController (match current pattern if useful)
- **Do not link:** UIKit, OpenGLES
- FFmpeg: Mac-built frameworks from `MAC_PORT/deps`

### Source inclusion differences vs tvOS

Keep:

- `tvosmain.mm` / `tvosplatform.cpp` (adapted) **or** rename later to `applemain` — rename is optional cosmetics
- `input/tvos/**`
- `radcontroller/tvoscontroller.cpp`
- `radfile/tvos/**`
- `pddi/gles/**` + `display_tvos/**`

Possibly add for Mac:

- `pddi/gles/glad/**` (if not using system/ANGLE headers alone)

Still exclude:

- Win32/PS2/GC/Xbox mains
- Desktop `pddi/gl/**` (unless abandoning GLES)
- Other platform controllers

### Post-build Mac asset copy

tvOS copies into flat `$WRAPPER_NAME/Assets/...`.

Mac should copy into something SDL_GetBasePath()-compatible, typically:

`Contents/Resources/Assets/TheSimpsons/...`

**or** beside the executable under `Contents/MacOS/Assets/...` (matches current `radTvosGetAppRoot` which does `SDL_GetBasePath()/Assets/TheSimpsons`).

**Audit recommendation:** put Assets next to the executable (`Contents/MacOS/Assets/...`) to minimize `tvosdrive.cpp` changes, **or** change pref/app root once and use Resources (cleaner Mac style). Pick one during implementation; document in notes.

---

## 9. Graphics strategy decision record (must choose before coding)

### Option 1 — ANGLE GLES2 (RECOMMENDED)

- Closest to current tvOS GLES2 code
- Keeps shaders / extensions mindset
- Uses Metal under the hood on M4
- Extra dep to build and embed

### Option 2 — SDL + glad GLES2 against a real ES context

- Tree already has glad GLES2
- Still needs an ES provider (often ANGLE anyway)

### Option 3 — Desktop OpenGL

- Native `OpenGL.framework` (deprecated but still present)
- More divergence from current GLES code
- Apple deprecation risk; worse long-term than ANGLE/Metal

**Blueprint default:** Option 1.

---

## 10. Controller-only plan (Mac)

### Keep

- `GameController.framework` extended gamepad path in `tvos_controller.mm`
- Mapping table in `tvoscontroller.cpp` (DPad/Start/A/B/X/Y/shoulders/triggers/sticks)
- Premake exclusion of Win32 mouse/keyboard controller screens

### Change

- Remove UIKit application-state logging dependency
- Ensure Mac app is allowed to receive GC events while focused (normal for GameController)
- Windowed mode: confirm controller still samples when app focused
- Optional: show a “connect controller” prompt using SDL message box instead of TV-oriented wait loop

### Explicit non-goals for MVP

- Keyboard drive / mouse camera
- Siri Remote style micro-gamepad (tvOS) — Mac users will use full gamepads

---

## 11. Audio / movie plan

### Audio

- Rebuild OpenAL Soft for Mac; keep existing radsound win32/OpenAL backend
- Keep streaming queue behavior (research.txt: do not break NPC stream path)
- CoreAudio/AudioToolbox remain system frameworks

### Movies

- Rebuild FFmpeg for Mac; keep `ffmpegmovieplayer.cpp`
- Prefer enabling VideoToolbox HW decode on Apple Silicon when configuring FFmpeg
- Re-test FMV after first boot (CPU decode fallback is OK for MVP)

---

## 12. Storage / sandbox differences

| Concern | tvOS today | Mac plan |
|---------|------------|----------|
| Assets | In app bundle `Assets/TheSimpsons` | Same logical layout; fix bundle copy destination |
| Saves | `~/Library/Caches/Radical/Simpsons` | Prefer `~/Library/Application Support/Radical/Simpsons` (migration helpers already in `tvosdrive.cpp`) |
| Backup exclusion xattr | Used | Harmless on Mac; can keep |
| Sandbox | tvOS app sandbox | For local USB debug builds, often no App Sandbox; if enabling sandbox later, add Mac entitlements for read/write and USB/controller as needed |

---

## 13. Phased implementation checklist (for after this audit)

### Phase 0 — Workspace (USB only)

- [x] Create blueprint in `MAC_PORT/` (this document)
- [ ] Create `MAC_PORT/deps/{src,prefix}`, `MAC_PORT/build`, `MAC_PORT/scripts`
- [ ] Snapshot note of current tvOS `otool -L` / `vtool` outputs (baseline)

### Phase 1 — Rebuild deps into `MAC_PORT/deps/prefix`

- [ ] SDL2 macosx arm64 (Cocoa + ANGLE/ES decision applied)
- [ ] OpenAL Soft macosx arm64 static
- [ ] libpng16 macosx arm64 static
- [ ] FFmpeg frameworks macosx arm64 (5 libs)
- [ ] ANGLE (if selected)
- [ ] Verify every binary: `platform MACOS`, `arm64`

### Phase 2 — Premake Mac target

- [ ] Add Mac Premake script / platform filters
- [ ] Point includes/libs to `MAC_PORT/deps/prefix`
- [ ] Swap frameworks (drop UIKit/OpenGLES; add AppKit/IOKit/ANGLE as needed)
- [ ] Mac Info.plist + rpath + embed frameworks postbuild
- [ ] Asset copy into Mac bundle layout

### Phase 3 — Minimal source adaptations

- [ ] Entry: remove `SDL_UIKitRunApp` for Mac
- [ ] `gl.hpp` Mac GLES headers path
- [ ] Controller: remove UIKit-only bits
- [ ] Pref path: Application Support on Mac
- [ ] Window: windowed/resizable defaults for desktop
- [ ] Define policy A (`RAD_TVOS` + `RAD_MACOS`) applied in Premake

### Phase 4 — Compile / link loop

- [ ] `premake5 xcode4` (Mac script)
- [ ] Build Debug arm64
- [ ] Fix missing symbols / wrong SDK includes until link succeeds

### Phase 5 — Bring-up tests

- [ ] Launch window + clear color / first frame
- [ ] Controller connect + menu navigate
- [ ] Frontend boot + asset open (watch VFS logs)
- [ ] Audio clip + streaming dialog
- [ ] FMV playback
- [ ] Save / load
- [ ] Retina drawable size sanity (not 1/4 screen)

### Phase 6 — Hardening (optional)

- [ ] Codesign ad-hoc / local
- [ ] Notarization (only if distributing off USB)
- [ ] Release config + LTO parity with tvOS Premake
- [ ] Strip tvOS-only diag spam if too noisy on Mac

---

## 14. Risk register

| Risk | Severity | Mitigation |
|------|----------|------------|
| GLES extensions missing under ANGLE | High | Feature-detect; keep OES fallbacks; test depth24/VAO |
| SDL rebuild without ANGLE leaves no ES context | High | Decide ES provider before Premake work |
| Silent use of tvOS libs via wrong libdirs | High | Separate `MAC_PORT/deps`; assert `vtool` platform MACOS in scripts |
| Retina / aspect issues | Medium | Use SDL drawable size; keep internal 1920x1080 FBO letterbox (tvOS display already FBO-based) |
| Controller focus / GC permission quirks on macOS versions | Medium | Test on macOS 15 host; keep GC weak-link pattern |
| FFmpeg ABI / framework ID mismatch | Medium | Rebuild all five together; embed + codesign consistently |
| Defining `RAD_TVOS` on Mac confuses future contributors | Low | Document Policy A in Premake comments + this file |
| USB performance slows compile | Low | Keep `MAC_PORT/build` on USB as required; use modest parallelism if I/O-bound |

---

## 15. Effort estimate (honest)

Assuming one engineer familiar with this repo:

| Work | Estimate |
|------|----------|
| Dep rebuild scripts + SDL/OpenAL/png/FFmpeg/ANGLE on USB | 1–2 days |
| Premake Mac platform + plist + embed/copy | 0.5–1 day |
| Source adaptations (entry/GL/input/FS) | 0.5–1 day |
| First successful link | same window as above |
| Bring-up to playable frontend/gameplay | 1–3 days (GL/asset/controller surprises) |

**Not** a month-long rewrite — but also **not** “just flip SDK to macosx.” The tvOS prebuilts and OpenGLES/UIKit shell are hard stops until rebuilt/adapted.

---

## 16. Non-negotiable rules for the implementation pass

1. **Everything stays on this USB** — deps, build products, scripts, notes under `MAC_PORT/` (plus necessary edits to project source when you leave audit-only mode).
2. **Do not destroy tvOS known-good** `third_party/` or `build/Debug/SRR2.app`.
3. **Controller-only** — keep GameController path; don’t regress to SDL pads unless GC fails.
4. **Verify platform tags** after every dep build (`vtool`/`otool`).
5. **No App Store / DRM / piracy scope creep** — this blueprint is about building your existing port for Mac silicon.

---

## 17. Immediate next step (when you say go)

1. Create `MAC_PORT/deps` + `MAC_PORT/scripts` scaffolding.
2. Write rebuild scripts for SDL2 (Cocoa+ANGLE), OpenAL, libpng, FFmpeg targeting `MAC_PORT/deps/prefix`.
3. Only after Mac libs verify as `platform MACOS`, start Premake/source adaptations.

This audit stops here by request: **blueprint only, no code changes.**
