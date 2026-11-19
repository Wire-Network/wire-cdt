FROM ubuntu:resolute
ENV TZ="America/New_York"
ENV DEBIAN_FRONTEND=noninteractive

# Builds with the Ubuntu 26.04 default toolchain: clang 21 over GCC 15's libstdc++.
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y \
    lsb-release          \
    wget                 \
    gnupg                \
    build-essential      \
    cmake                \
    git                  \
    jq                   \
    libcurl4-openssl-dev \
    libgmp-dev           \
    ninja-build          \
    python3-numpy        \
    file                 \
    zlib1g-dev           \
    zstd                 \
    curl                 \
    zip                  \
    unzip                \
    tar                  \
    sudo                 \
    golang               \
    python3-dev          \
    libffi-dev           \
    libedit-dev          \
    libxml2-dev          \
    libncurses-dev       \
    libzstd-dev          \
    libssl-dev           \
    ccache               \
    clang-21             \
    clang-tools-21       \
    autoconf automake libtool

ENV CC=/usr/bin/clang-21
ENV CXX=/usr/bin/clang++-21
ENV CMAKE_MAKE_PROGRAM=/usr/bin/ninja
