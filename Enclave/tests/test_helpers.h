/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif

#include "qcbor/UsefulBuf.h"
#include "qcbor/qcbor_encode.h"
#include "csuit/suit_digest.h"
#include "csuit/suit_manifest_process.h"

#ifdef __cplusplus
}
#undef delete
#endif

UsefulBufC tc_manager_test_helper_encode_suit_digest(int64_t alg_id,
                                                     UsefulBufC digest_bytes,
                                                     uint8_t *encoded_buf,
                                                     size_t encoded_buf_len);

UsefulBufC tc_manager_test_helper_calculate_sha256_and_encode_suit_digest(
                                                     const uint8_t *data,
                                                     size_t data_len,
                                                     uint8_t *digest_buf,
                                                     size_t digest_buf_len,
                                                     uint8_t *encoded_buf,
                                                     size_t encoded_buf_len);

UsefulBufC tc_manager_test_helper_encode_component_id(const char *name,
                                                      uint8_t *buf,
                                                      size_t buf_size);

int tc_manager_test_helper_store_complete_record(UsefulBufC manifest_digest,
                                                 uint64_t manifest_sequence_number,
                                                 const char *manifest_name,
                                                 const char *wapp_name,
                                                 const char *wapp_bin_text);

#endif /* TEST_HELPERS_H */
