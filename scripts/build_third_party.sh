#!/usr/bin/env bash

#
# * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
# *
# * SPDX-License-Identifier: BSD-2-Clause
#

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
OUT_LIB_DIR="${ROOT_DIR}/lib"

SGXSSL_ROOT_DIR="${THIRD_PARTY_DIR}/intel-sgx-ssl"
SGXSSL_OPENSSL_SOURCE_DIR="${SGXSSL_ROOT_DIR}/openssl_source"
SGXSSL_DIR="${SGXSSL_ROOT_DIR}/Linux"
SGXSSL_PACKAGE_DIR="${SGXSSL_DIR}/package"
SGXSSL_INC="${SGXSSL_PACKAGE_DIR}/include"
SGXSSL_LIB="${SGXSSL_PACKAGE_DIR}/lib64"
QCBOR_INC_DIR="${THIRD_PARTY_DIR}/QCBOR/inc"
T_COSE_INC_DIR="${THIRD_PARTY_DIR}/t_cose/inc"
WAMR_DIR="${THIRD_PARTY_DIR}/wasm-micro-runtime"
WAMR_SGX_PLATFORM_DIR="${WAMR_DIR}/product-mini/platforms/linux-sgx"

mkdir -p "${OUT_LIB_DIR}"

require_dir() {
  local path="$1"
  if [[ ! -d "${path}" ]]; then
    echo "[ERROR] missing directory: ${path}" >&2
    exit 1
  fi
}

require_dir "${THIRD_PARTY_DIR}"
require_dir "${QCBOR_INC_DIR}"
require_dir "${T_COSE_INC_DIR}"
require_dir "${SGXSSL_OPENSSL_SOURCE_DIR}"
require_dir "${SGXSSL_DIR}"
require_dir "${WAMR_DIR}"
require_dir "${WAMR_SGX_PLATFORM_DIR}"

build_sgxssl() {
  echo "[INFO] Building SGXSSL..."
  pushd "${SGXSSL_OPENSSL_SOURCE_DIR}"
  if [[ ! -f openssl-3.0.18.tar.gz ]]; then
    wget https://github.com/openssl/openssl/releases/download/openssl-3.0.18/openssl-3.0.18.tar.gz
  fi
  popd

  pushd "${SGXSSL_DIR}"
  ./build_openssl.sh
  make SGX_MODE=SIM
  popd

  require_dir "${SGXSSL_INC}"
  require_dir "${SGXSSL_LIB}"
  cp -a "${SGXSSL_LIB}/." "${OUT_LIB_DIR}/"
}

build_wamr() {
  echo "[INFO] Building wasm-micro-runtime (linux-sgx)..."
  source /opt/intel/sgxsdk/environment
  pushd "${WAMR_SGX_PLATFORM_DIR}"
  rm -rf build
  mkdir -p build
  pushd build
  cmake .. -DWAMR_BUILD_DUMP_CALL_STACK=1 
  make
  popd
  popd
}


build_qcbor() {
  echo "[INFO] Building QCBOR..."
  make -C "${THIRD_PARTY_DIR}/QCBOR" -B libqcbor.a
  cp "${THIRD_PARTY_DIR}/QCBOR/libqcbor.a" "${OUT_LIB_DIR}/"
}

build_t_cose() {
  echo "[INFO] Building t_cose..."
  make -C "${THIRD_PARTY_DIR}/t_cose" -B -f Makefile.ossl libt_cose.a \
    QCBOR_INC="-I ${QCBOR_INC_DIR}" \
    CRYPTO_INC="-I ${SGXSSL_INC}" \
    CRYPTO_LIB="-L ${SGXSSL_LIB} -lcrypto" \
    CMD_LINE="-g"
  cp "${THIRD_PARTY_DIR}/t_cose/libt_cose.a" "${OUT_LIB_DIR}/"
}

build_libcsuit() {
  echo "[INFO] Building libcsuit..."
  make -C "${THIRD_PARTY_DIR}/libcsuit" -B -f Makefile libcsuit.a \
    CFLAGS="-Os -g -I ${SGXSSL_INC} -D_FORTIFY_SOURCE=0 -U_FORTIFY_SOURCE"
  cp "${THIRD_PARTY_DIR}/libcsuit/libcsuit.a" "${OUT_LIB_DIR}/"
}

build_libteep() {
  echo "[INFO] Building libteep..."
  make -C "${THIRD_PARTY_DIR}/libteep" -B -f Makefile libteep.a \
    CMD_INC="-I ${QCBOR_INC_DIR} -I ${T_COSE_INC_DIR} -I ${SGXSSL_INC} -I ./inc" \
    CMD_LD="-L ${SGXSSL_LIB} -lcrypto "
  cp "${THIRD_PARTY_DIR}/libteep/libteep.a" "${OUT_LIB_DIR}/"
}

build_sgxssl
build_wamr
build_qcbor
build_t_cose
build_libcsuit
build_libteep
