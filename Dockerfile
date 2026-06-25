#
# Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
#
# SPDX-License-Identifier: BSD-2-Clause
#

FROM sgx_sample_deb

ARG DEBIAN_FRONTEND=noninteractive
ARG GO_VERSION=1.22.12
ARG AZ_DCAP_CLIENT_VERSION=1.13.1
ARG NODE_MAJOR=24
ARG TAWS_DCAP_PROVIDER

USER root

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    cracklib-runtime \
    curl \
    debhelper \
    devscripts \
    dh-make \
    dpkg-dev \
    fakeroot \
    gcc \
    g++ \
    git \
    gnupg \
    lsb-release \
    make \
    netcat-openbsd \
    openssl \
    perl \
    pkgconf \
    build-essential \
    libboost-dev \
    libboost-system-dev \
    libboost-thread-dev \
    libcurl4-openssl-dev \
    libprotobuf-c-dev \
    libsgx-ae-pce \
    libsgx-enclave-common \
    libsgx-headers \
    libssl-dev \
    protobuf-c-compiler \
    protobuf-compiler \
    python-is-python3 \
    wget \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN if [ "${TAWS_DCAP_PROVIDER}" = "azure" ]; then \
        apt-get update \
        && apt-get install -y --no-install-recommends nlohmann-json3-dev; \
    else \
        apt-get update \
        && apt-get install -y --no-install-recommends libsgx-aesm-quote-ex-plugin \
        && curl -fsSL "https://deb.nodesource.com/setup_${NODE_MAJOR}.x" -o /tmp/nodesource_setup.sh \
        && bash /tmp/nodesource_setup.sh \
        && apt-get update \
        && apt-get install -y --no-install-recommends nodejs \
        && rm -f /tmp/nodesource_setup.sh; \
    fi \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" -O /tmp/go.tar.gz \
    && rm -rf /usr/local/go \
    && tar -C /usr/local -xzf /tmp/go.tar.gz \
    && rm -f /tmp/go.tar.gz

RUN if [ "${TAWS_DCAP_PROVIDER}" = "azure" ]; then \
        git clone --depth 1 --branch "${AZ_DCAP_CLIENT_VERSION}" --recurse-submodules https://github.com/microsoft/Azure-DCAP-Client.git /tmp/azure-dcap-client \
        && cd /tmp/azure-dcap-client/src/Linux \
        && ./configure \
        && make \
        && make install \
        && ldconfig \
        && rm -rf /tmp/azure-dcap-client; \
    fi

ENV PATH="/usr/local/go/bin:${PATH}"

WORKDIR /work/taws
COPY . /work/taws/

RUN bash -lc "set -euo pipefail \
    && source /opt/intel/sgxsdk/environment \
    && cd /work/taws/third_party/intel-dcap/QuoteGeneration \
    && ./download_prebuilt.sh \
    && if [ \"${TAWS_DCAP_PROVIDER}\" = azure ]; then \
        BUILD_PLATFORM=docker make deb_sgx_pce_logic_pkg deb_sgx_qe3_logic_pkg deb_sgx_ae_qe3_pkg deb_sgx_ae_id_enclave_pkg deb_sgx_dcap_ql_pkg \
        && dpkg -i --force-overwrite \
        installer/linux/deb/libsgx-pce-logic/libsgx-pce-logic_*.deb \
        installer/linux/deb/libsgx-qe3-logic/libsgx-qe3-logic_*.deb \
        installer/linux/deb/libsgx-ae-qe3/libsgx-ae-qe3_*.deb \
        installer/linux/deb/libsgx-ae-id-enclave/libsgx-ae-id-enclave_*.deb \
        installer/linux/deb/libsgx-dcap-ql/libsgx-dcap-ql_*.deb \
        installer/linux/deb/libsgx-dcap-ql/libsgx-dcap-ql-dev_*.deb; \
    else \
        sed -i 's#grep -qE '\''docker|lxc'\'' /proc/1/cgroup#grep -qE '\''docker|lxc'\'' /proc/1/cgroup || [ -f /.dockerenv ]#' pccs/service/startup.sh \
        && sed -i 's/exit 5/exit 0/' pccs/service/startup.sh \
        && BUILD_PLATFORM=docker make deb_sgx_pce_logic_pkg deb_sgx_qe3_logic_pkg deb_sgx_ae_qe3_pkg deb_sgx_ae_id_enclave_pkg deb_sgx_dcap_ql_pkg deb_sgx_dcap_default_qpl_pkg deb_sgx_dcap_pccs_pkg \
        && dpkg -i --force-overwrite --ignore-depends=npm \
        installer/linux/deb/libsgx-pce-logic/libsgx-pce-logic_*.deb \
        installer/linux/deb/libsgx-qe3-logic/libsgx-qe3-logic_*.deb \
        installer/linux/deb/libsgx-ae-qe3/libsgx-ae-qe3_*.deb \
        installer/linux/deb/libsgx-ae-id-enclave/libsgx-ae-id-enclave_*.deb \
        installer/linux/deb/libsgx-dcap-ql/libsgx-dcap-ql_*.deb \
        installer/linux/deb/libsgx-dcap-ql/libsgx-dcap-ql-dev_*.deb \
        installer/linux/deb/libsgx-dcap-default-qpl/libsgx-dcap-default-qpl_*.deb \
        installer/linux/deb/libsgx-dcap-default-qpl/libsgx-dcap-default-qpl-dev_*.deb \
        pccs/build_infrastructure/installer/linux/deb/sgx-dcap-pccs/sgx-dcap-pccs_*.deb \
        && cd /opt/intel/sgx-dcap-pccs \
        && npm ci --omit=dev; \
    fi \
    && cd /work/taws/scripts \
    && ./build_third_party.sh \
    && cd /work/taws \
    && make SGX_MODE=HW SGX_DEBUG=1"

COPY scripts/start_sgx_services.sh /usr/local/bin/start-sgx-services
RUN chmod 0755 /usr/local/bin/start-sgx-services

ENV SGX_MODE=HW
ENV TAWS_DCAP_PROVIDER=${TAWS_DCAP_PROVIDER}
ENV TAWS_WEB_ADDR=0.0.0.0:8181
ENV TAWS_TAM_URL=http://localhost:8080/tam
ENV PCCS_CACHING_MODE=LAZY

EXPOSE 8181

ENTRYPOINT ["/usr/local/bin/start-sgx-services"]
CMD ["bash", "-lc", "source /opt/intel/sgxsdk/environment && cd /work/taws && exec ./build/go/taws web --addr \"${TAWS_WEB_ADDR}\" --url \"${TAWS_TAM_URL}\""]
