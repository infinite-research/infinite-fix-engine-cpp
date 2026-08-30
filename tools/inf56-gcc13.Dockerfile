FROM ubuntu@sha256:d78ab76437b1afc5f01e223d6bf0172763f404bb166441328845adbef44518cb

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq \
    && apt-get install -y --no-install-recommends \
      autoconf=2.71-3 \
      automake=1:1.16.5-1.3ubuntu1 \
      binutils=2.42-4ubuntu2.10 \
      ca-certificates=20260601~24.04.1 \
      cmake=3.28.3-1build7 \
      coreutils=9.4-3ubuntu6.2 \
      curl=8.5.0-2ubuntu10.13 \
      g++-13=13.3.0-6ubuntu2~24.04.1 \
      gcc-13=13.3.0-6ubuntu2~24.04.1 \
      git=1:2.43.0-1ubuntu7.3 \
      gnupg=2.4.4-2ubuntu17.4 \
      gzip=1.12-1ubuntu3.2 \
      libtool=2.4.7-7build1 \
      make=4.3-4.1build2 \
      ninja-build=1.11.1-2 \
      psmisc=23.7-1build1 \
      ruby=1:3.2~ubuntu1 \
      tar=1.35+dfsg-3ubuntu0.4 \
      xz-utils=5.6.1+really5.4.5-1ubuntu0.3 \
    && curl -fsS --proto '=https' --tlsv1.2 \
      https://apt.llvm.org/llvm-snapshot.gpg.key \
      -o /usr/share/keyrings/llvm-snapshot.gpg.key \
    && echo '8b2a587ffd672c4687e7581dad4b2f6c1bb2ad6b480cd9771ba2ff48e0b8c75d  /usr/share/keyrings/llvm-snapshot.gpg.key' | sha256sum -c - \
    && gpg --dearmor --output /usr/share/keyrings/llvm-snapshot.gpg \
      /usr/share/keyrings/llvm-snapshot.gpg.key \
    && echo 'deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-22 main' \
      > /etc/apt/sources.list.d/llvm22.list \
    && apt-get update -qq \
    && apt-get install -y --no-install-recommends \
      clang-format-22=1:22.1.8~++20260714014902+ca7933e47d3a-1~exp1~20260714135019.80 \
    && rm -rf /var/lib/apt/lists/* /usr/share/keyrings/llvm-snapshot.gpg.key
