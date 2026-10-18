FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    build-essential \
    cmake \
    python3-pip \
    && apt-get clean

RUN pip3 install "conan>=2,<3"

RUN apt-get update && apt-get install -y \
    clangd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
