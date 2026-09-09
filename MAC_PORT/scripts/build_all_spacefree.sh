#!/usr/bin/env bash
# Build Mac arm64 deps on USB using space-free paths, install into MAC_PORT/deps/prefix.
set -euo pipefail

FFROOT="/Volumes/Untitled2/MAC/srr2mac_build"
PREFIX_FINAL="/Volumes/Untitled2/MAC/New project/The-Simpsons-Hit-and-Run-Version-3/MAC_PORT/deps/prefix"
PREFIX="$FFROOT/prefix"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

mkdir -p "$FFROOT" "$PREFIX"/{include,lib,Frameworks,bin,share} "$PREFIX_FINAL"

echo "==> FFROOT=$FFROOT"
echo "==> PREFIX=$PREFIX -> sync to $PREFIX_FINAL"
echo "==> JOBS=$JOBS TARGET=$MACOSX_DEPLOYMENT_TARGET"
date

sync_to_mac_port() {
  echo "==> Syncing prefix into MAC_PORT/deps/prefix ..."
  mkdir -p "$PREFIX_FINAL"
  rsync -a "$PREFIX/" "$PREFIX_FINAL/"
}

build_sdl2() {
  echo "==> Building SDL2..."
  cmake -S "$FFROOT/SDL2" -B "$FFROOT/SDL2-build" \
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
  cmake --build "$FFROOT/SDL2-build" -j"$JOBS"
  cmake --install "$FFROOT/SDL2-build"
}

build_openal() {
  echo "==> Building OpenAL Soft..."
  cmake -S "$FFROOT/OpenALSoft" -B "$FFROOT/OpenALSoft-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DLIBTYPE=STATIC \
    -DALSOFT_UTILS=OFF \
    -DALSOFT_EXAMPLES=OFF \
    -DALSOFT_INSTALL_EXAMPLES=OFF \
    -DALSOFT_INSTALL_UTILS=OFF \
    -DALSOFT_BACKEND_WAVE=ON \
    -DALSOFT_REQUIRE_COREAUDIO=ON
  cmake --build "$FFROOT/OpenALSoft-build" -j"$JOBS"
  cmake --install "$FFROOT/OpenALSoft-build"
}

make_framework() {
  local name="$1"
  local dylib="$2"
  local fw="$PREFIX/Frameworks/${name}.framework"
  rm -rf "$fw"
  mkdir -p "$fw"
  cp "$dylib" "$fw/$name"
  chmod +x "$fw/$name"
  install_name_tool -id "@rpath/${name}.framework/${name}" "$fw/$name" || true
  local dep old
  for dep in libavcodec libavformat libavutil libswresample libswscale libavfilter libavdevice; do
    if otool -L "$fw/$name" 2>/dev/null | grep -q "${dep}\\."; then
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
  <key>CFBundleExecutable</key><string>${name}</string>
  <key>CFBundleIdentifier</key><string>com.simpsonsmac.srr2.${name}</string>
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
  echo "==> Building FFmpeg..."
  local FFSRC="$FFROOT/ffmpeg"
  local BLD="$FFROOT/ffmpeg-build"
  rm -rf "$BLD"
  mkdir -p "$BLD"
  pushd "$BLD" >/dev/null
  "$FFSRC/configure" \
    --prefix="$PREFIX" \
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

  echo "==> Wrapping FFmpeg dylibs as frameworks..."
  local libdir="$PREFIX/lib"
  local name dylib real
  for name in libavcodec libavformat libavutil libswresample libswscale; do
    dylib="$libdir/${name}.dylib"
    if [[ ! -e "$dylib" ]]; then
      echo "ERROR: missing $dylib" >&2
      ls -la "$libdir" >&2
      exit 1
    fi
    real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$dylib")"
    make_framework "$name" "$real"
  done
}

verify() {
  echo "==> Verifying..."
  local f
  for f in \
    "$PREFIX_FINAL/lib/libSDL2.a" \
    "$PREFIX_FINAL/lib/libopenal.a" \
    "$PREFIX_FINAL/lib/libpng16.a" \
    "$PREFIX_FINAL/Frameworks/libavcodec.framework/libavcodec"
  do
    echo "---- $f"
    lipo -info "$f"
    vtool -show-build "$f" 2>/dev/null | head -12 || true
  done
  # Ensure OpenAL headers layout
  ls -la "$PREFIX_FINAL/include/AL" 2>/dev/null || ls -la "$PREFIX_FINAL/include" | head
  date
  echo "==> ALL DEPS OK"
}

# libpng already built into PREFIX_FINAL earlier; copy into space-free prefix too if present
if [[ -f "$PREFIX_FINAL/lib/libpng16.a" ]]; then
  mkdir -p "$PREFIX/lib" "$PREFIX/include"
  rsync -a "$PREFIX_FINAL/lib/libpng"* "$PREFIX/lib/" 2>/dev/null || true
  rsync -a "$PREFIX_FINAL/include/" "$PREFIX/include/" 2>/dev/null || true
fi

build_sdl2
build_openal
build_ffmpeg
sync_to_mac_port
verify
