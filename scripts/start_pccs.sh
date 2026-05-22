#!/usr/bin/env bash
#
# Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
#
# SPDX-License-Identifier: BSD-2-Clause
#

set -euo pipefail

PCCS_HOME=/opt/intel/sgx-dcap-pccs
PCCS_CONFIG="${PCCS_HOME}/config/default.json"
PCCS_SSL_DIR="${PCCS_HOME}/ssl_key"
QCNL_CONFIG=/etc/sgx_default_qcnl.conf
PCCS_PID=

cleanup() {
    if [ -n "${PCCS_PID}" ] && kill -0 "${PCCS_PID}" >/dev/null 2>&1; then
        kill "${PCCS_PID}"
        wait "${PCCS_PID}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

configure_pccs() {
    mkdir -p "${PCCS_SSL_DIR}" "${PCCS_HOME}/logs"

    if [ ! -f "${PCCS_SSL_DIR}/private.pem" ] || [ ! -f "${PCCS_SSL_DIR}/file.crt" ]; then
        openssl genrsa -out "${PCCS_SSL_DIR}/private.pem" 2048
        openssl req -new -key "${PCCS_SSL_DIR}/private.pem" -out "${PCCS_SSL_DIR}/csr.pem" -subj "/CN=localhost"
        openssl x509 -req -days 365 -in "${PCCS_SSL_DIR}/csr.pem" -signkey "${PCCS_SSL_DIR}/private.pem" -out "${PCCS_SSL_DIR}/file.crt"
        rm -f "${PCCS_SSL_DIR}/csr.pem"
    fi

    node - "${PCCS_CONFIG}" <<'NODE'
const fs = require('fs');
const configPath = process.argv[2];
const rawConfig = fs.readFileSync(configPath, 'utf8');
const normalizedConfig = rawConfig
    .split(/\r?\n/)
    .filter((line) => !line.trimStart().startsWith('//'))
    .join('\n')
    .replace(/,\s*([}\]])/g, '$1');
const config = JSON.parse(normalizedConfig);

config.HTTPS_PORT = 8081;
config.hosts = '127.0.0.1';
config.ApiKey = process.env.PCCS_API_KEY || config.ApiKey || '';
config.proxy = process.env.PCCS_PROXY || config.proxy || '';
config.CachingFillMode = process.env.PCCS_CACHING_MODE || config.CachingFillMode || 'LAZY';
config.DB_CONFIG = config.DB_CONFIG || 'sqlite';
config.sqlite = config.sqlite || {};
config.sqlite.options = config.sqlite.options || {};
config.sqlite.options.storage = config.sqlite.options.storage || '/opt/intel/sgx-dcap-pccs/pckcache.db';

fs.writeFileSync(configPath, `${JSON.stringify(config, null, 4)}\n`);
NODE

    chown -R pccs:pccs "${PCCS_HOME}"
    chmod 0600 "${PCCS_SSL_DIR}/private.pem"
    chmod 0644 "${PCCS_SSL_DIR}/file.crt"
    chmod 0640 "${PCCS_CONFIG}"
}

configure_qcnl() {
    cat > "${QCNL_CONFIG}" <<'EOF'
{
  "pccs_url": "https://localhost:8081/sgx/certification/v4/",
  "use_secure_cert": false,
  "pccs_api_version": "3.1"
}
EOF
}

start_pccs() {
    cd "${PCCS_HOME}"
    /bin/su -s /bin/bash pccs -c "NODE_ENV=production /usr/bin/node ./pccs_server.js" &
    PCCS_PID=$!

    for _ in $(seq 1 60); do
        if nc -z 127.0.0.1 8081 >/dev/null 2>&1; then
            return 0
        fi
        if ! kill -0 "${PCCS_PID}" >/dev/null 2>&1; then
            wait "${PCCS_PID}"
        fi
        sleep 1
    done

    echo "PCCS did not start listening on 127.0.0.1:8081" >&2
    return 1
}

main() {
    configure_pccs
    configure_qcnl
    start_pccs

    exec "$@"
}

main "$@"
