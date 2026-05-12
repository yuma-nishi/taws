/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcap_quote_ocalls.h"

static bool require_dcap(void)
{
    const char *value = getenv("REQUIRE_DCAP");
    return value != NULL && strcmp(value, "1") == 0;
}

static void print_hex(const char *label, const uint8_t *bytes, size_t len)
{
    printf("%s=", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", bytes[i]);
    }
    printf("\n");
}

static void print_qe_target_info(const sgx_target_info_t *qe_target_info)
{
    printf("qe_target_info:\n");
    print_hex("  mr_enclave",
              qe_target_info->mr_enclave.m,
              sizeof(qe_target_info->mr_enclave.m));
    printf("  attributes.flags=0x%016llx\n",
           (unsigned long long)qe_target_info->attributes.flags);
    printf("  attributes.xfrm=0x%016llx\n",
           (unsigned long long)qe_target_info->attributes.xfrm);
    printf("  config_svn=%u\n", (unsigned int)qe_target_info->config_svn);
    printf("  misc_select=0x%08x\n", qe_target_info->misc_select);
    print_hex("  config_id",
              qe_target_info->config_id,
              sizeof(qe_target_info->config_id));
}

int main(void)
{
    assert(ocall_get_qe_target_info(NULL) == SGX_ERROR_INVALID_PARAMETER);
    assert(ocall_get_quote_size(NULL) == SGX_ERROR_INVALID_PARAMETER);
    assert(ocall_get_quote(NULL, NULL, 0) == SGX_ERROR_INVALID_PARAMETER);
    printf("[PASS] DCAP OCALL wrappers reject invalid parameters\n");

    sgx_target_info_t qe_target_info;
    memset(&qe_target_info, 0, sizeof(qe_target_info));
    sgx_status_t sgx_ret = ocall_get_qe_target_info(&qe_target_info);
    if (sgx_ret != SGX_SUCCESS) {
        fprintf(stderr,
                "[SKIP] ocall_get_qe_target_info returned 0x%04x. "
                "DCAP quote provider is not available in this environment.\n",
                sgx_ret);
        return require_dcap() ? 1 : 0;
    }
    print_qe_target_info(&qe_target_info);

    uint32_t quote_size = 0;
    sgx_ret = ocall_get_quote_size(&quote_size);
    if (sgx_ret != SGX_SUCCESS || quote_size == 0) {
        fprintf(stderr,
                "[FAIL] ocall_get_quote_size returned status=0x%04x size=%u\n",
                sgx_ret,
                quote_size);
        return 1;
    }

    printf("[PASS] DCAP OCALL wrappers reached real QE target info and quote size paths; quote_size=%u\n",
           quote_size);
    return 0;
}
