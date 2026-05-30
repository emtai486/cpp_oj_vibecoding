FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    libpq-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S judge-worker -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --target judge-worker -j"$(nproc)" 


FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    ca-certificates \
    docker.io \
    libpq5 \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /src/build/judge-worker /app/judge-worker

ENV TESTDATA_ROOT=/app/testdata
ENV JUDGE_WORKSPACE_ROOT=/app/judge-workspaces
ENV JUDGE_WORKSPACE_HOST_ROOT=/tmp/oj-judge-workspaces
ENV JUDGE_SANDBOX_IMAGE=gcc:13-bookworm

CMD ["/app/judge-worker"]
