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

#ifndef TEST_ENCLAVE_FILENAME
#define TEST_ENCLAVE_FILENAME "build/create_evidence_dcap_integration/create_evidence_dcap_integration_enclave.signed.so"
#endif

#ifndef QUOTE_OUTPUT_FILENAME
#define QUOTE_OUTPUT_FILENAME "quote.dat"
#endif

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

static void print_hex(const char *label, const uint8_t *bytes, size_t len)
{
    printf("%s=", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", bytes[i]);
    }
    printf("\n");
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
                "[SKIP] create_evidence_dcap returned TEEP error %d. "
                "DCAP quote provider may not be available in this environment.\n",
                retval);
        return require_dcap() ? 1 : 0;
    }

    assert(actual_len > 0);
    assert(actual_len <= sizeof(evidence));
    assert(contains_nonzero_byte(evidence, actual_len));

    if (!write_file(QUOTE_OUTPUT_FILENAME, evidence, actual_len)) {
        fprintf(stderr, "[FAIL] failed to write %s\n", QUOTE_OUTPUT_FILENAME);
        return 1;
    }
    printf("quote_file=%s\n", QUOTE_OUTPUT_FILENAME);
    printf("[PASS] create_evidence_dcap generated a real DCAP quote; quote_size=%zu\n",
           actual_len);
    return 0;
}
