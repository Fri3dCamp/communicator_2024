#-------------------------------------------------------------------------------
# WCH-IC RISC-V Toolchain Devcontainer
# Copyright © 2024 Bert Outtier
#-------------------------------------------------------------------------------

# Base image: Ubuntu Dev Container
FROM --platform=linux/amd64 mcr.microsoft.com/devcontainers/base:ubuntu

# Root user for setup
USER root

# Dependencies setup
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    cu \
    curl \
    make \
    software-properties-common \
    tar \
    udev \
    unzip \
    usbutils \
    && rm -rf /var/lib/apt/lists/*

# Setup dir for packages installation
WORKDIR /tmp

#- Risc-V GCC ----------------------------------------------
ARG TOOLCHAIN_URL="https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v12.2.0-3/xpack-riscv-none-elf-gcc-12.2.0-3-linux-x64.tar.gz"
ARG TOOLCHAIN_INSTALL_DIR="/opt" 

RUN curl -sLO ${TOOLCHAIN_URL} && \
    mkdir -p ${TOOLCHAIN_INSTALL_DIR} && \
    tar -xf $(basename ${TOOLCHAIN_URL}) -C ${TOOLCHAIN_INSTALL_DIR} && \
    rm -rf $(basename ${TOOLCHAIN_URL})
ENV PATH=$PATH:${TOOLCHAIN_INSTALL_DIR}/xpack-riscv-none-elf-gcc-12.2.0-3/bin

#- User setup ------------------------------------------------------------------
# Add plugdev group for non-root debugger access
RUN usermod -aG plugdev vscode

USER vscode

VOLUME [ "/workspaces" ]
WORKDIR /workspaces