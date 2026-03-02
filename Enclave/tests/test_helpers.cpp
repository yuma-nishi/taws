/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "test_helpers.h"
#include "tc_manager.h"
#include <string.h>

UsefulBufC tc_manager_test_helper_encode_suit_digest(int64_t alg_id,
                                                     UsefulBufC digest_bytes,
                                                     uint8_t *encoded_buf,
                                                     size_t encoded_buf_len)
{
    UsefulBuf out = {encoded_buf, encoded_buf_len};
    QCBOREncodeContext ctx;
    UsefulBufC encoded = NULLUsefulBufC;

    QCBOREncode_Init(&ctx, out);
    QCBOREncode_OpenArray(&ctx);
    QCBOREncode_AddInt64(&ctx, alg_id);
    QCBOREncode_AddBytes(&ctx, digest_bytes);
    QCBOREncode_CloseArray(&ctx);
    if (QCBOREncode_Finish(&ctx, &encoded) != QCBOR_SUCCESS) {
        return NULLUsefulBufC;
    }
    return encoded;
}

static UsefulBufC tc_manager_test_helper_compute_sha256_digest(const uint8_t *data,
                                                                size_t data_len,
                                                                uint8_t *digest_buf,
                                                                size_t digest_buf_len)
{
    UsefulBufC payload = {data, data_len};
    UsefulBuf digest = {digest_buf, digest_buf_len};
    if (suit_generate_sha256(payload, digest) != SUIT_SUCCESS) {
        return NULLUsefulBufC;
    }
    return UsefulBuf_Const(digest);
}

UsefulBufC tc_manager_test_helper_calculate_sha256_and_encode_suit_digest(
                                                     const uint8_t *data,
                                                     size_t data_len,
                                                     uint8_t *digest_buf,
                                                     size_t digest_buf_len,
                                                     uint8_t *encoded_buf,
                                                     size_t encoded_buf_len)
{
    UsefulBufC digest = tc_manager_test_helper_compute_sha256_digest(data, data_len,
                                                                     digest_buf, digest_buf_len);
    if (UsefulBuf_IsNULLOrEmptyC(digest)) {
        return NULLUsefulBufC;
    }
    return tc_manager_test_helper_encode_suit_digest(SUIT_ALGORITHM_ID_SHA256,
                                                     digest,
                                                     encoded_buf,
                                                     encoded_buf_len);
}

UsefulBufC tc_manager_test_helper_encode_component_id(const char *name,
                                                      uint8_t *buf,
                                                      size_t buf_size)
{
    if (name == NULL || buf == NULL || buf_size == 0) {
        return NULLUsefulBufC;
    }

    QCBOREncodeContext context;
    UsefulBuf out = { buf, buf_size };
    UsefulBufC encoded = NULLUsefulBufC;
    UsefulBufC component_name = { name, strlen(name) };

    QCBOREncode_Init(&context, out);
    QCBOREncode_OpenArray(&context);
    QCBOREncode_AddBytes(&context, component_name);
    QCBOREncode_CloseArray(&context);
    if (QCBOREncode_Finish(&context, &encoded) != QCBOR_SUCCESS) {
        return NULLUsefulBufC;
    }
    return encoded;
}

int tc_manager_test_helper_store_complete_record(UsefulBufC manifest_digest,
                                                 uint64_t manifest_sequence_number,
                                                 const char *manifest_name,
                                                 const char *wapp_name,
                                                 const char *wapp_bin_text)
{
    if (manifest_name == NULL || wapp_name == NULL || wapp_bin_text == NULL) {
        return -1;
    }

    uint8_t manifest_component_id_buf[SUIT_MAX_NAME_LENGTH + 8] = {0};
    uint8_t wapp_component_id_buf[SUIT_MAX_NAME_LENGTH + 8] = {0};
    UsefulBufC manifest_component_id =
        tc_manager_test_helper_encode_component_id(manifest_name,
                                                   manifest_component_id_buf,
                                                   sizeof(manifest_component_id_buf));
    UsefulBufC wapp_component_id =
        tc_manager_test_helper_encode_component_id(wapp_name,
                                                   wapp_component_id_buf,
                                                   sizeof(wapp_component_id_buf));
    if (UsefulBuf_IsNULLOrEmptyC(manifest_component_id) ||
        UsefulBuf_IsNULLOrEmptyC(wapp_component_id)) {
        return -1;
    }

    suit_store_args_t args = {};
    args.operation = SUIT_STORE;
    args.manifest_digest = manifest_digest;
    args.manifest_sequence_number = manifest_sequence_number;
    args.is_manifest_itself = true;
    args.dst = manifest_component_id;
    args.src_buf.ptr = (const uint8_t *)"manifest-binary";
    args.src_buf.len = strlen((const char *)args.src_buf.ptr);
    if (tc_manager_store_record_from_store_args(&args) != 0) {
        return -1;
    }

    memset(&args, 0, sizeof(args));
    args.operation = SUIT_STORE;
    args.manifest_digest = manifest_digest;
    args.is_manifest_itself = false;
    args.dst = wapp_component_id;
    args.src_buf.ptr = (const uint8_t *)wapp_bin_text;
    args.src_buf.len = strlen((const char *)args.src_buf.ptr);
    if (tc_manager_store_record_from_store_args(&args) != 0) {
        return -1;
    }

    if (tc_manager_check_and_update_record(manifest_digest) != 0) {
        return -1;
    }
    return 0;
}
