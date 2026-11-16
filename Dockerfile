# ZeroBounce C API – test image (CMake + Unity tests)
# Same Ubuntu release as the ubuntu-latest CI runner (ubuntu-26.04).
# The base is pinned by digest (the multi-arch index of ubuntu:26.04); Dependabot moves it.
FROM ubuntu:26.04@sha256:d66b366dac61b575bee2aef07e9c26e8070096fbe84a3d4d3eb0da069b5d76db

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
    && ./build/bin/ZeroBounceTests \
    && ./build/bin/ZeroBounceTransportTests

RUN useradd --uid 65532 --create-home --shell /usr/sbin/nologin zbtest \
    && chown -R zbtest:zbtest /app

USER zbtest

CMD ["sh", "-c", "build/bin/ZeroBounceTests && build/bin/ZeroBounceTransportTests"]
