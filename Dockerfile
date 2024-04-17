FROM ubuntu:22.04
CMD bash
RUN apt-get update -y
RUN apt install -y libtbb2-dev \
    build-essential\
    cmake \
    git \
    clang \
    libaio-dev \
    libsnappy-dev \
    zlib1g-dev \
    libbz2-dev \
    liblz4-dev \
    libzstd-dev \
    librocksdb-dev \
    liblmdb++-dev \
    libwiredtiger-dev \ 
    liburing-dev \
