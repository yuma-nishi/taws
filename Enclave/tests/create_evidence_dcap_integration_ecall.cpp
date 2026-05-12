/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "teep_create_evidence.h"

static const uint8_t k_public_key[] = {
    0x04 /* POINT_CONVERSION_UNCOMPRESSED */,
    0x58, 0x86, 0xcd, 0x61, 0xdd, 0x87, 0x58, 0x62,
    0xe5, 0xaa, 0xa8, 0x20, 0xe7, 0xa1, 0x52, 0x74,
    0xc9, 0x68, 0xa9, 0xbc, 0x96, 0x04, 0x8d, 0xdc,
    0xac, 0xe3, 0x2f, 0x50, 0xc3, 0x65, 0x1b, 0xa3,
    0x9e, 0xed, 0x81, 0x25, 0xe9, 0x32, 0xcd, 0x60,
    0xc0, 0xea, 0xd3, 0x65, 0x0d, 0x0a, 0x48, 0x5c,
    0xf7, 0x26, 0xd3, 0x78, 0xd1, 0xb0, 0x16, 0xed,
    0x42, 0x98, 0xb2, 0x96, 0x1e, 0x25, 0x8f, 0x1b
};

static const uint8_t k_challenge[] = {0xCA, 0xFE, 0xBA, 0xBE};

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

    teep_err_t result = create_evidence_dcap(&query_request,
                                             evidence_storage,
                                             &key_pair,
                                             &evidence);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    *actual_len = evidence.len;
    return TEEP_SUCCESS;
}
