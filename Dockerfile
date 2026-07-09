#
# Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
#
# SPDX-License-Identifier: BSD-2-Clause
#

FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG GO_VERSION=1.22.12
ARG AZ_DCAP_CLIENT_VERSION=1.13.1
ARG TAWS_DCAP_PROVIDER
ARG SGX_SDK_URL=https://download.01.org/intel-sgx/sgx-linux/2.29/distro/ubuntu22.04-server/sgx_linux_x64_sdk_2.29.100.1.bin
ARG SGX_DEB_REPO_URL=https://download.01.org/intel-sgx/latest/dcap-latest/linux/distro/ubuntu22.04-server/sgx_debian_local_repo.tgz

USER root

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    debhelper \
    gnupg \
    lsb-release \
    libcurl4-openssl-dev \
    perl \
    pkgconf \
    python-is-python3 \
    wget \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://deb.nodesource.com/setup_24.x | bash - \
    && apt-get update \
    && apt-get install -y --no-install-recommends nodejs \
    && node --version \
    && npm --version \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q "${SGX_SDK_URL}" -O /tmp/sgx_linux_x64_sdk.bin \
    && chmod 0755 /tmp/sgx_linux_x64_sdk.bin \
    && bash /tmp/sgx_linux_x64_sdk.bin --prefix=/opt/intel \
    && rm -f /tmp/sgx_linux_x64_sdk.bin

RUN wget -q "${SGX_DEB_REPO_URL}" -O /tmp/sgx_debian_local_repo.tgz \
    && mkdir -p /opt/intel/sgx_debian_local_repo \
    && tar -xzf /tmp/sgx_debian_local_repo.tgz -C /opt/intel/sgx_debian_local_repo --strip-components=1 \
    && printf 'deb [trusted=yes] file:///opt/intel/sgx_debian_local_repo jammy main\n' > /etc/apt/sources.list.d/intel-sgx-local.list \
    && rm -f /tmp/sgx_debian_local_repo.tgz

RUN if [ "${TAWS_DCAP_PROVIDER}" = "azure" ]; then \
        wget -q https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb -O /tmp/packages-microsoft-prod.deb \
        && dpkg -i /tmp/packages-microsoft-prod.deb \
        && rm -f /tmp/packages-microsoft-prod.deb \
        && apt-get update \
        && apt-get install -y --no-install-recommends \
        az-dcap-client="${AZ_DCAP_CLIENT_VERSION}" \
        libsgx-ae-id-enclave \
        libsgx-ae-pce \
        libsgx-ae-qe3 \
        libsgx-dcap-ql \
        libsgx-dcap-ql-dev \
        libsgx-headers \
        libsgx-aesm-quote-ex-plugin \
        libsgx-urts; \
    else \
        apt-get update \
        && apt-get install -y --no-install-recommends \
        cracklib-runtime \
        libsgx-ae-id-enclave \
        libsgx-ae-pce \
        libsgx-ae-qe3 \
        libsgx-aesm-quote-ex-plugin \
        libsgx-dcap-default-qpl \
        libsgx-dcap-default-qpl-dev \
        libsgx-dcap-ql \
        libsgx-dcap-ql-dev \
        libsgx-headers \
        libsgx-urts \
        netcat-openbsd \
        openssl \
        sgx-aesm-service \
        # Keep PCCS package as the upstream app source, but bypass host-style
        # service startup assumptions that fail during container installation.
        && apt-get download sgx-dcap-pccs \
        && mkdir -p /tmp/sgx-dcap-pccs \
        && dpkg-deb -R ./sgx-dcap-pccs_*.deb /tmp/sgx-dcap-pccs \
        && sed -i 's/exit 5/exit 0/' /tmp/sgx-dcap-pccs/opt/intel/sgx-dcap-pccs/startup.sh \
        && dpkg-deb -b /tmp/sgx-dcap-pccs /tmp/sgx-dcap-pccs.deb \
        && dpkg -i /tmp/sgx-dcap-pccs.deb \
        && cd /opt/intel/sgx-dcap-pccs \
        # Install runtime-only Node dependencies for direct `node pccs_server.js`.
        && NODE_ENV=production npm install --omit=dev --no-audit --no-fund \
        && rm -rf /tmp/sgx-dcap-pccs /tmp/sgx-dcap-pccs.deb ./sgx-dcap-pccs_*.deb; \
    fi \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" -O /tmp/go.tar.gz \
    && rm -rf /usr/local/go \
    && tar -C /usr/local -xzf /tmp/go.tar.gz \
    && rm -f /tmp/go.tar.gz

ENV PATH="/usr/local/go/bin:${PATH}"

WORKDIR /work/taws

COPY scripts/build_third_party.sh /work/taws/scripts/build_third_party.sh
COPY third_party/intel-sgx-ssl /work/taws/third_party/intel-sgx-ssl
COPY third_party/QCBOR /work/taws/third_party/QCBOR
COPY third_party/t_cose /work/taws/third_party/t_cose
COPY third_party/libcsuit /work/taws/third_party/libcsuit
COPY third_party/libteep /work/taws/third_party/libteep
COPY third_party/wasm-micro-runtime /work/taws/third_party/wasm-micro-runtime

RUN bash -lc "set -euo pipefail \
    && source /opt/intel/sgxsdk/environment \
    && cd /work/taws/scripts \
    && ./build_third_party.sh"

COPY App /work/taws/App
COPY Enclave /work/taws/Enclave
COPY common /work/taws/common
COPY yolov8-frontend /work/taws/yolov8-frontend
COPY Makefile go.mod /work/taws/

RUN bash -lc "set -euo pipefail \
    && source /opt/intel/sgxsdk/environment \
    && cd /work/taws \
    && make SGX_MODE=HW SGX_DEBUG=1"

COPY scripts/start_sgx_services.sh /usr/local/bin/start-sgx-services
RUN chmod 0755 /usr/local/bin/start-sgx-services

ENV SGX_MODE=HW
ENV TAWS_DCAP_PROVIDER=${TAWS_DCAP_PROVIDER}
ENV TAWS_WEB_ADDR=0.0.0.0:8181
ENV TAWS_TAM_URL=http://localhost:8080/tam
ENV TAWS_LOG_LEVEL=info
ENV PCCS_CACHING_MODE=LAZY
ENV PCCS_LOG_LEVEL=error

EXPOSE 8181

ENTRYPOINT ["/usr/local/bin/start-sgx-services"]
CMD ["bash", "-lc", "source /opt/intel/sgxsdk/environment && cd /work/taws && exec ./build/go/taws web --addr \"${TAWS_WEB_ADDR}\" --url \"${TAWS_TAM_URL}\" --log-level \"${TAWS_LOG_LEVEL}\""]
