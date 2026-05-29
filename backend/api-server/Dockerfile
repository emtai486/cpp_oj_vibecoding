FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libcpp-httplib-dev \
    libpq-dev \
    libssl-dev \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --target api-server -j"$(nproc)"

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates libcpp-httplib0.14t64 libpq5 openssl \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/api-server/api-server /app/api-server
COPY openapi /app/openapi

ENV API_HOST=0.0.0.0
ENV API_PORT=8080
ENV OPENAPI_PATH=/app/openapi/openapi.json
ENV TESTDATA_ROOT=/app/testdata

EXPOSE 8080

CMD ["/app/api-server"]
