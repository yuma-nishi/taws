#!/usr/bin/env bash
#
# Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
#
# SPDX-License-Identifier: BSD-2-Clause
#

set -euo pipefail

AESM_HOME=/opt/intel/sgxpsw/aesm
QCNL_CONFIG=/etc/sgx_default_qcnl.conf
AESM_PCCS_URL="${AESM_PCCS_URL:-https://localhost:8081/sgx/certification/v4/}"

cd "${AESM_HOME}"

ln -sfn libdcap_quoteprov.so libdcap_quoteprov.so.1
ln -sfn libsgx_default_qcnl_wrapper.so libsgx_default_qcnl_wrapper.so.1

cat > "${QCNL_CONFIG}" <<EOF
{
  "pccs_url": "${AESM_PCCS_URL}",
  "use_secure_cert": false,
  "pccs_api_version": "3.1"
}
EOF

exec ./aesm_service --no-daemon
