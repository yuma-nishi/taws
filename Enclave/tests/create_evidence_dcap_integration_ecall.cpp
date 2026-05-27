/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "create_evidence_dcap_integration_t.h"
#include "teep_create_evidence.h"

static const uint8_t k_public_key[] = {
    0x04 /* POINT_CONVERSION_UNCOMPRESSED */,
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

extern "C" int printf(const char *fmt, ...)
{
    char buf[BUFSIZ] = {'\0'};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    ocall_print_string(buf);
    return 0;
}

extern "C" int ecall_test_create_evidence_dcap(uint8_t *evidence_buf,
                                                size_t evidence_buf_len,
                                                size_t *actual_len)
{
    if (actual_len == NULL) {
        return TEEP_ERR_INVALID_VALUE;
    }
    *actual_len = 0;

    if (evidence_buf == NULL || evidence_buf_len == 0) {
        return TEEP_ERR_INVALID_VALUE;
    }

    teep_query_request_t query_request;
    teep_key_t key_pair;
    memset(&query_request, 0, sizeof(query_request));
    memset(&key_pair, 0, sizeof(key_pair));

    query_request.contains = TEEP_MESSAGE_CONTAINS_CHALLENGE;
    query_request.challenge.len = sizeof(k_challenge);
    query_request.challenge.ptr = k_challenge;
    key_pair.public_key = k_public_key;
    key_pair.public_key_len = sizeof(k_public_key);

    UsefulBuf evidence_storage = {
        .ptr = evidence_buf,
        .len = evidence_buf_len,
    };
    UsefulBufC evidence = NULLUsefulBufC;

    teep_err_t result = create_evidence_dcap_envelope(&query_request,
                                                      evidence_storage,
                                                      &key_pair,
                                                      &evidence);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    *actual_len = evidence.len;
    return TEEP_SUCCESS;
}
