FROM debian:11

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    clang-11 \
    cmake \
    curl \
    file \
    gcc \
    git \
    g++ \
    libclang-11-dev \
    libclang-cpp11-dev \
    libedit-dev \
    libgmp10 \
    libgmp-dev \
    llvm-11-dev \
    llvm-11 \
    llvm-11-tools \
    locales \
    make \
    mawk \
    m4 \
    perl \
    pkg-config \
    protobuf-compiler \
    python-setuptools \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    wget \
    && rm -rf /var/lib/apt/lists/*

ENV CHPL_VERSION 1.28.0
ENV CHPL_HOME    /opt/chapel/$CHPL_VERSION
ENV CHPL_GMP     system
ENV CHPL_LLVM    system


RUN mkdir -p /opt/chapel \
    && wget -q -O - https://github.com/chapel-lang/chapel/releases/download/$CHPL_VERSION/chapel-$CHPL_VERSION.tar.gz | tar -xzC /opt/chapel --transform 's/chapel-//' \
    && make -C $CHPL_HOME \
    && make -C $CHPL_HOME chpldoc test-venv mason \
    && make -C $CHPL_HOME cleanall

# Configure locale
RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && \
    echo 'LANG="en_US.UTF-8"'>/etc/default/locale && \
    dpkg-reconfigure --frontend=noninteractive locales && \
    update-locale LANG=en_US.UTF-8

# Configure dummy git user
RUN git config --global user.email "noreply@example.com" && \
    git config --global user.name  "Chapel user"


COPY entrypoint.sh .
RUN chmod a+x /entrypoint.sh

# setting path based on the underlying architecture
ENTRYPOINT ["/entrypoint.sh"]
