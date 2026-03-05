FROM sgx_sample_deb

ARG DEBIAN_FRONTEND=noninteractive
ARG GO_VERSION=1.22.12

USER root

RUN apt-get update && apt-get install -y --no-install-recommends \
    automake \
    autoconf \
    build-essential \
    ca-certificates \
    cmake \
    gcc \
    git \
    g++ \
    libssl-dev \
    libtool \
    make \
    ocaml \
    ocamlbuild \
    perl \
    pkg-config \
    python-is-python3 \
    wget \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" -O /tmp/go.tar.gz \
    && rm -rf /usr/local/go \
    && tar -C /usr/local -xzf /tmp/go.tar.gz \
    && rm -f /tmp/go.tar.gz

ENV PATH="/usr/local/go/bin:${PATH}"

WORKDIR /work/taws
COPY . /work/taws/

RUN bash -lc "source /opt/intel/sgxsdk/environment && cd scripts && ./build_third_party.sh && cd .. && make SGX_MODE=SIM"

ENV SGX_MODE=SIM

EXPOSE 8081

CMD ["bash", "-lc", "source /opt/intel/sgxsdk/environment && ./build/go/taws web"]
