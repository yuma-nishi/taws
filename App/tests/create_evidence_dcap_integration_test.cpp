/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sgx_error.h"
#include "sgx_urts.h"
#include "create_evidence_dcap_integration_u.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "qcbor/qcbor_decode.h"
#ifdef __cplusplus
}
#endif

#ifndef TEST_ENCLAVE_FILENAME
#define TEST_ENCLAVE_FILENAME "build/create_evidence_dcap_integration/create_evidence_dcap_integration_enclave.signed.so"
#endif

#ifndef EVIDENCE_OUTPUT_FILENAME
#define EVIDENCE_OUTPUT_FILENAME "evidence.dat"
#endif

static const uint8_t k_public_key[] = {
    0x04,
    0xbe, 0x7c, 0x56, 0x99, 0x3f, 0x71, 0x11, 0x45,
    0x34, 0xc2, 0xf4, 0xa4, 0xf4, 0xe4, 0x60, 0x67,
    0x84, 0xfa, 0x9d, 0x96, 0x35, 0xe1, 0x22, 0xbc,
    0x8a, 0x49, 0x0b, 0x2e, 0x11, 0xfe, 0xb9, 0x32,
    0x81, 0x69, 0x6b, 0x42, 0xc3, 0xbe, 0x1b, 0x24,
    0x4c, 0xc0, 0x3b, 0xca, 0x97, 0xf0, 0xce, 0x75,
    0xe2, 0xd9, 0x3a, 0xda, 0x1c, 0xe5, 0x56, 0x62,
    0x92, 0x27, 0xf1, 0x0a, 0x8c, 0x2c, 0x5b, 0x29
};

static const uint8_t k_challenge[] = {
    0x94, 0x8f, 0x88, 0x60, 0xd1, 0x3a, 0x46, 0x3e,
    0x8e
};

static bool require_dcap(void)
{
    const char *value = getenv("REQUIRE_DCAP");
    return value != NULL && strcmp(value, "1") == 0;
}

static bool contains_nonzero_byte(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) {
            return true;
        }
    }
    return false;
}

static bool write_file(const char *path, const uint8_t *bytes, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }

    size_t written = fwrite(bytes, 1, len, fp);
    bool ok = (written == len);
    if (fclose(fp) != 0) {
        ok = false;
    }
    return ok;
}

extern "C" void ocall_print_string(const char *str)
{
    if (str != NULL) {
        fputs(str, stdout);
    }
}

static void verify_attestation_payload(const uint8_t *evidence, size_t evidence_len)
{
    UsefulBufC encoded_payload = {
        .ptr = evidence,
        .len = evidence_len,
    };
    QCBORDecodeContext decode_context;
    QCBORItem item = {};
    QCBORDecode_Init(&decode_context, encoded_payload, QCBOR_DECODE_MODE_NORMAL);

    assert(QCBORDecode_GetNext(&decode_context, &item) == QCBOR_SUCCESS);
    assert(item.uDataType == QCBOR_TYPE_ARRAY);
    assert(item.val.uCount == 2);

    assert(QCBORDecode_GetNext(&decode_context, &item) == QCBOR_SUCCESS);
    assert(item.uDataType == QCBOR_TYPE_BYTE_STRING);
    assert(item.val.string.len > 0);
    assert(contains_nonzero_byte(static_cast<const uint8_t *>(item.val.string.ptr),
                                 item.val.string.len));

    assert(QCBORDecode_GetNext(&decode_context, &item) == QCBOR_SUCCESS);
    assert(item.uDataType == QCBOR_TYPE_BYTE_STRING);
    UsefulBufC raw_report_data = item.val.string;
    assert(raw_report_data.len == 64 + sizeof(k_challenge));

    const uint8_t *report_data = static_cast<const uint8_t *>(raw_report_data.ptr);
    assert(memcmp(report_data, k_public_key + 1, 32) == 0);
    assert(memcmp(report_data + 32, k_public_key + 33, 32) == 0);
    assert(memcmp(report_data + 64, k_challenge, sizeof(k_challenge)) == 0);

    assert(QCBORDecode_Finish(&decode_context) == QCBOR_SUCCESS);
}

int main(void)
{
    sgx_enclave_id_t eid = 0;
    sgx_status_t sgx_ret = sgx_create_enclave(TEST_ENCLAVE_FILENAME,
                                              SGX_DEBUG_FLAG,
                                              NULL,
                                              NULL,
                                              &eid,
                                              NULL);
    if (sgx_ret != SGX_SUCCESS) {
        fprintf(stderr,
                "[SKIP] sgx_create_enclave(%s) returned 0x%04x. "
                "SGX HW runtime may not be available in this environment.\n",
                TEST_ENCLAVE_FILENAME,
                sgx_ret);
        return require_dcap() ? 1 : 0;
    }

    uint8_t evidence[16384] = {};
    size_t actual_len = 0;
    int retval = 0;
    sgx_ret = ecall_test_create_evidence_dcap(eid,
                                              &retval,
                                              evidence,
                                              sizeof(evidence),
                                              &actual_len);

    sgx_destroy_enclave(eid);

    if (sgx_ret != SGX_SUCCESS) {
        fprintf(stderr,
                "[FAIL] ecall_test_create_evidence_dcap returned SGX status 0x%04x\n",
                sgx_ret);
        return 1;
    }

    if (retval != 0) {
        fprintf(stderr,
                "[SKIP] create_evidence_dcap_envelope returned TEEP error %d. "
                "DCAP quote provider may not be available in this environment.\n",
                retval);
        return require_dcap() ? 1 : 0;
    }

    assert(actual_len > 0);
    assert(actual_len <= sizeof(evidence));
    assert(contains_nonzero_byte(evidence, actual_len));
    verify_attestation_payload(evidence, actual_len);

    if (!write_file(EVIDENCE_OUTPUT_FILENAME, evidence, actual_len)) {
        fprintf(stderr, "[FAIL] failed to write %s\n", EVIDENCE_OUTPUT_FILENAME);
        return 1;
    }
    printf("evidence_file=%s\n", EVIDENCE_OUTPUT_FILENAME);
    printf("[PASS] create_evidence_dcap_envelope generated a DCAP attestation payload; evidence_size=%zu\n",
           actual_len);
    return 0;
}
