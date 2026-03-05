#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAWS_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPS_DIR="${TAWS_ROOT}/.deps"
SGX_REPO_DIR="${DEPS_DIR}/confidential-computing.sgx"

mkdir -p "${DEPS_DIR}"

if [[ ! -d "${SGX_REPO_DIR}" ]]; then
  echo "[INFO] cloning confidential-computing.sgx..."
  git clone https://github.com/intel/confidential-computing.sgx.git "${SGX_REPO_DIR}"
fi

echo "[INFO] preparing confidential-computing.sgx..."
(
  cd "${SGX_REPO_DIR}"
  make preparation

  echo "[INFO] building SGX SDK base image: sgx_sample_deb"
  cd docker/build
  docker build --target sample_deb \
    --build-arg https_proxy="${https_proxy:-}" \
    --build-arg http_proxy="${http_proxy:-}" \
    -t sgx_sample_deb \
    -f ./Dockerfile \
    ../../
)

echo "[INFO] building taws image: taws-sim"
(
  cd "${TAWS_ROOT}"
  docker build -t taws-sim .
)

echo "[INFO] done"
echo "  - base image: sgx_sample_deb"
echo "  - taws image: taws-sim"
