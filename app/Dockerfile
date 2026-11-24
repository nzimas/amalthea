FROM ubuntu:20.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends g++ cmake make pkg-config libsdl2-dev libasound2-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /work
