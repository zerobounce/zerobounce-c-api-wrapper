# ZeroBounce C API – test image (CMake + Unity tests)
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build \
    && ./build/bin/ZeroBounceTests

RUN useradd --uid 65532 --create-home --shell /usr/sbin/nologin zbtest \
    && chown -R zbtest:zbtest /app

USER zbtest

CMD ["build/bin/ZeroBounceTests"]
