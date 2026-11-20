#!/bin/bash
# Runs inside the build container.
#   /src  : the repository (read-only bind mount from Windows)
#   /deps : docker volume caching app/deps/work (FFmpeg, SDL3, dav1d, libusb, adb)
#   /out  : bind mount receiving the finished win64 distribution
set -euo pipefail

# Build on the container filesystem: the Windows bind mount is slow and
# would leak CRLF/permission issues into the build.
rsync -a --delete \
    --exclude '.git/' \
    --exclude 'app/deps/work/' \
    --exclude 'release/work/' \
    --exclude 'release/output/' \
    --exclude 'build*/' \
    /src/ /build/

# Reuse the cached third-party builds
mkdir -p /deps
ln -sfn /deps /build/app/deps/work

cd /build

# scrcpy-cam unit tests that do not need FFmpeg/SDL (run natively)
gcc -std=c11 -Wall -Wextra -Werror -Iapp/src -o /tmp/test_capture_geometry \
    app/tests/test_capture_geometry.c app/src/capture_geometry.c
/tmp/test_capture_geometry

# .git is not copied, so release/build_common cannot run "git describe"
export VERSION="${VERSION:-5.0.1-cam}"
bash release/build_windows.sh 64

DIST=/build/release/work/build-win64/dist
# Replace the content only: the directory itself may be in use on the Windows
# side (e.g. current directory of a shell)
mkdir -p /out/scrcpy-cam-win64
find /out/scrcpy-cam-win64 -mindepth 1 -delete
cp -r "$DIST"/. /out/scrcpy-cam-win64/
# Launcher and emergency script next to scrcpy.exe: the folder is self-contained
cp tools/scrcpy-cam.ps1 tools/scrcpy-usb.cmd /out/scrcpy-cam-win64/
echo "Fertig: /out/scrcpy-cam-win64"
