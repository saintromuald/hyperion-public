# The Linux build's toolchain: clang, CMake and Ninja, and the development
# packages SDL3 builds its X11, Wayland, sound and Vulkan backends against. The same file
# builds for x86-64 and for ARM64 (docker buildx --platform linux/arm64).
#
#     docker build -t hyperion-linux engine/tools/linux
#     docker buildx build --platform linux/arm64 --load -t hyperion-linux-arm64 engine/tools/linux
#     python engine/tools/build_engine_linux.py      (runs the build in it)
#
# Ubuntu 22.04, so the program needs glibc 2.35 or newer and runs on that release and the
# distributions of its time or later (a Jetson's JetPack 6 is 22.04). Its own clang is 14,
# so clang 18 comes from apt.llvm.org, the version the build was first made with, and the
# C++ library from GCC 12, which has C++20's; both are linked into the program statically.
ARG BASE=ubuntu:22.04
FROM ${BASE}

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        wget gnupg lsb-release software-properties-common ca-certificates \
        cmake ninja-build pkg-config git python3 make file \
        g++-12 libstdc++-12-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev \
        libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
        libegl1-mesa-dev libgl-dev libgbm-dev libdrm-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev \
        libvulkan-dev mesa-vulkan-drivers vulkan-tools \
        libdbus-1-dev libudev-dev libibus-1.0-dev \
    && wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh && bash /tmp/llvm.sh 18 \
    && apt-get install -y --no-install-recommends lld-18 llvm-18 \
    && for t in clang clang++ llvm-nm llvm-ar llvm-ranlib llvm-strip llvm-objcopy llvm-cxxfilt \
                llvm-objdump llvm-readelf ld.lld lld; do \
           ln -sf /usr/bin/$t-18 /usr/bin/$t; done \
    && rm -rf /var/lib/apt/lists/* /tmp/llvm.sh

WORKDIR /src
