FROM amalthea-build:ubuntu20
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends e2fsprogs cpio wget bzip2 bc libncurses-dev strace python3 binutils && rm -rf /var/lib/apt/lists/*
WORKDIR /work
