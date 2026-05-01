FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Install C++ build toolchain required to build this project (C++20, CMake,
# and git for CMake FetchContent of doctest).
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        g++ \
        gdb \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*
