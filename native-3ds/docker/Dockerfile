# Linux build environment for the Melee 3DS builder.
# Everything the build downloads lives in the container's /work volume, so
# `docker rmi` + `docker volume rm` removes every trace (see docker/README.md).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive PYTHONDONTWRITEBYTECODE=1
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git curl xz-utils rsync \
        build-essential pkg-config zlib1g-dev libpng-dev \
        python3 python3-pil python3-numpy \
    && rm -rf /var/lib/apt/lists/*

COPY docker/entrypoint.sh /usr/local/bin/melee-build
RUN chmod +x /usr/local/bin/melee-build
WORKDIR /work
ENTRYPOINT ["melee-build"]
