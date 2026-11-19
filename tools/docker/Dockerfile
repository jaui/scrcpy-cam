# Cross-compile environment for the scrcpy-cam Windows client (win64).
# Package list follows doc/build.md ("Cross-compile from Linux to Windows")
# plus the tools needed by app/deps/*.sh (wget, shasum, make, cmake, nasm, unzip).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        ca-certificates git wget curl unzip xz-utils rsync \
        perl make cmake nasm pkg-config meson ninja-build python3 \
        autoconf automake libtool \
        mingw-w64 mingw-w64-tools libz-mingw-w64-dev \
 && rm -rf /var/lib/apt/lists/*

COPY build.sh /usr/local/bin/scrcpy-cam-build
RUN chmod +x /usr/local/bin/scrcpy-cam-build

WORKDIR /build
ENTRYPOINT ["/usr/local/bin/scrcpy-cam-build"]
