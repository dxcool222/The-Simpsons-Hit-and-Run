#!/usr/bin/env bash
# Build Mac arm64 deps into MAC_PORT/deps/prefix (USB only).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAC_PORT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$MAC_PORT/deps/src"
PREFIX="$MAC_PORT/deps/prefix"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
export CMAKE_OSX_ARCHITECTURES=arm64
export CMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET"

mkdir -p "$PREFIX"/{include,lib,Frameworks,bin,share} "$SRC"

echo "==> PREFIX=$PREFIX"
echo "==> JOBS=$JOBS TARGET=$MACOSX_DEPLOYMENT_TARGET"

build_sdl2() {
  echo "==> Building SDL2 (static, Cocoa, OpenGL)..."
  cmake -S "$SRC/SDL2" -B "$SRC/SDL2-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DSDL_SHARED=OFF \
    -DSDL_STATIC=ON \
    -DSDL_TEST=OFF \
    -DSDL_TESTS=OFF \
    -DSDL_OPENGL=ON \
    -DSDL_OPENGLES=ON \
    -DSDL_METAL=ON \
    -DSDL_FRAMEWORK=OFF \
    -DSDL_STATIC_PIC=ON
  cmake --build "$SRC/SDL2-build" -j"$JOBS"
  cmake --install "$SRC/SDL2-build"
}

build_openal() {
  echo "==> Building OpenAL Soft (static)..."
  cmake -S "$SRC/OpenALSoft" -B "$SRC/OpenALSoft-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DLIBTYPE=STATIC \
    -DALSOFT_UTILS=OFF \
    -DALSOFT_EXAMPLES=OFF \
    -DALSOFT_INSTALL_EXAMPLES=OFF \
    -DALSOFT_INSTALL_UTILS=OFF \
    -DALSOFT_BACKEND_WAVE=ON \
    -DALSOFT_REQUIRE_COREAUDIO=ON
  cmake --build "$SRC/OpenALSoft-build" -j"$JOBS"
  cmake --install "$SRC/OpenALSoft-build"
}

build_libpng() {
  echo "==> Building libpng (static)..."
  cmake -S "$SRC/libpng" -B "$SRC/libpng-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DPNG_SHARED=OFF \
    -DPNG_STATIC=ON \
    -DPNG_TESTS=OFF \
    -DPNG_TOOLS=OFF \
    -DZLIB_ROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr
  cmake --build "$SRC/libpng-build" -j"$JOBS"
  cmake --install "$SRC/libpng-build"
}

make_framework() {
  local name="$1"
  local dylib="$2"
  local fw="$PREFIX/Frameworks/${name}.framework"
  local ver
  ver="$(basename "$dylib")"
  rm -rf "$fw"
  mkdir -p "$fw"
  cp "$dylib" "$fw/$name"
  chmod +x "$fw/$name"
  install_name_tool -id "@rpath/${name}.framework/${name}" "$fw/$name" || true
  # Fix internal deps to @rpath frameworks
  local dep
  for dep in libavcodec libavformat libavutil libswresample libswscale libavfilter libavdevice; do
    if otool -L "$fw/$name" 2>/dev/null | grep -q "${dep}."; then
      local old
      old="$(otool -L "$fw/$name" | awk '/'"${dep}"'\./{print $1; exit}')"
      if [[ -n "$old" && "$old" != *"@rpath"* ]]; then
        install_name_tool -change "$old" "@rpath/${dep}.framework/${dep}" "$fw/$name" || true
      fi
    fi
  done
  cat > "$fw/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>English</string>
  <key>CFBundleExecutable</key><string>${name}</string>
  <key>CFBundleIdentifier</key><string>com.simpsonsmac.srr2.${name}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>${name}</string>
  <key>CFBundlePackageType</key><string>FMWK</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
</dict>
</plist>
EOF
  echo "    made $fw"
}

build_ffmpeg() {
  echo "==> Building FFmpeg (shared, macos arm64)..."
  # FFmpeg configure cannot handle whitespace in paths. Use space-free USB tree.
  local FFROOT="/Volumes/Untitled2/MAC/srr2mac_build"
  local FFSRC="$FFROOT/ffmpeg"
  local BLD="$FFROOT/ffmpeg-build"
  local FFPREFIX="$FFROOT/prefix"
  if [[ ! -d "$FFSRC" ]]; then
    echo "ERROR: missing $FFSRC (clone FFmpeg into space-free USB path)" >&2
    exit 1
  fi
  rm -rf "$BLD"
  mkdir -p "$BLD" "$FFPREFIX"
  pushd "$BLD" >/dev/null
  "$FFSRC/configure" \
    --prefix="$FFPREFIX" \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-programs \
    --disable-debug \
    --enable-gpl \
    --enable-network \
    --enable-videotoolbox \
    --enable-audiotoolbox \
    --disable-xlib \
    --disable-libxcb \
    --disable-bzlib \
    --disable-lzma \
    --disable-libxml2 \
    --disable-mediafoundation \
    --arch=arm64 \
    --extra-cflags="-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}" \
    --extra-ldflags="-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}"
  make -j"$JOBS"
  make install
  popd >/dev/null

  echo "==> Syncing FFmpeg install into MAC_PORT prefix..."
  mkdir -p "$PREFIX"/{include,lib,bin,share}
  rsync -a "$FFPREFIX/include/" "$PREFIX/include/"
  rsync -a "$FFPREFIX/lib/" "$PREFIX/lib/"
  rsync -a "$FFPREFIX/bin/" "$PREFIX/bin/" 2>/dev/null || true
  rsync -a "$FFPREFIX/share/" "$PREFIX/share/" 2>/dev/null || true

  echo "==> Wrapping FFmpeg dylibs as frameworks..."
  # Map installed dylibs (versioned names) into framework bundles
  local libdir="$PREFIX/lib"
  for name in libavcodec libavformat libavutil libswresample libswscale; do
    local dylib
    dylib="$(ls -1 "$libdir"/${name}.*.dylib 2>/dev/null | grep -v '\.[0-9]*\.[0-9]*\.' | head -1 || true)"
    if [[ -z "$dylib" ]]; then
      dylib="$(ls -1 "$libdir"/${name}.dylib 2>/dev/null | head -1 || true)"
    fi
    # Prefer the unversioned symlink target if present
    if [[ -L "$libdir/${name}.dylib" ]]; then
      dylib="$libdir/${name}.dylib"
    fi
    if [[ -z "$dylib" || ! -e "$dylib" ]]; then
      echo "ERROR: missing $name dylib in $libdir" >&2
      ls -la "$libdir" | head -50 >&2
      exit 1
    fi
    # Resolve to real file for copy
    local real
    real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$dylib")"
    make_framework "$name" "$real"
  done
}

verify() {
  echo "==> Verifying platform tags (expect MACOS / arm64)..."
  local f
  for f in \
    "$PREFIX/lib/libSDL2.a" \
    "$PREFIX/lib/libopenal.a" \
    "$PREFIX/lib/libpng16.a" \
    "$PREFIX/Frameworks/libavcodec.framework/libavcodec"
  do
    echo "---- $f"
    lipo -info "$f"
    vtool -show-build "$f" 2>/dev/null | head -15 || otool -l "$f" | awk '/LC_BUILD_VERSION/{p=1} p&&/platform|minos|sdk/{print} /^Load command/{if(p&&seen++){exit}}'
  done
  echo "==> Deps done."
}

case "${1:-all}" in
  sdl2) build_sdl2 ;;
  openal) build_openal ;;
  libpng) build_libpng ;;
  ffmpeg) build_ffmpeg ;;
  verify) verify ;;
  all)
    build_sdl2
    build_openal
    build_libpng
    build_ffmpeg
    verify
    ;;
  *)
    echo "usage: $0 [all|sdl2|openal|libpng|ffmpeg|verify]"
    exit 2
    ;;
esac
