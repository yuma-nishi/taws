/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <assert.h>
#include <string.h>

#include "test_helpers.h"

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

#include "tc_manager.h"

extern "C" suit_err_t __wrap_suit_condition_callback(suit_condition_args_t condition_args,
                                                     suit_callback_ret_t *condition_ret);
extern "C" suit_err_t __wrap_suit_store_callback(suit_store_args_t store_args);

static void tc_manager_test_helper_store_manifest_only(UsefulBufC digest,
                                                       suit_store_args_t *store_args)
{
    //echo "['manifest-bin']" | diag2cbor.rb | xxd -p
    static const uint8_t manifest_bin[] = {
        0x81, 0x4b, 0x6d, 0x61, 0x6e, 0x69, 0x66, 0x65, 0x74, 0x2d, 0x62, 0x69, 0x6e
    };
    store_args->operation = SUIT_STORE;
    store_args->is_manifest_itself = true;
    store_args->manifest_digest = digest;
    //echo "['manifet-bin']" | diag2cbor.rb | xxd -p
    store_args->src_buf.ptr = (uint8_t *)manifest_bin;
    store_args->src_buf.len = sizeof(manifest_bin);
}

static void tc_manager_test_helper_store_payload(UsefulBufC data, UsefulBufC digest, suit_store_args_t *store_args)
{
    store_args->operation = SUIT_STORE;
    store_args->src_buf = data;
    store_args->is_manifest_itself = false;
    store_args->manifest_digest = digest;
}

static void test_image_match_without_store_app(void)
{
    // When no payload was stored, image match should fail as unsupported.
    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);
    suit_store_args_t store_args = {};

    tc_manager_test_helper_store_manifest_only(manifest_digest, &store_args);
    assert(tc_manager_store_record_from_store_args(&store_args) == 0);

    suit_condition_args_t args = {};

    //echo "['wapp-bin']" | diag2cbor.rb | xxd -p
    const uint8_t wapp_bin[] = {0x81, 0x48, 0x77, 0x61, 0x70, 0x70, 0x2d, 0x62, 0x69, 0x6e};
    uint8_t wapp_hash_bytes[SHA256_DIGEST_LENGTH] = {0};
    uint8_t encoded_buf[128] = {0};
    UsefulBufC encoded_wapp_hash = tc_manager_test_helper_calculate_sha256_and_encode_suit_digest(
                                                        wapp_bin,
                                                        sizeof(wapp_bin),
                                                        wapp_hash_bytes,
                                                        sizeof(wapp_hash_bytes),
                                                        encoded_buf,
                                                        sizeof(encoded_buf));

    args.condition = SUIT_CONDITION_IMAGE_MATCH;
    args.expected.u64 = 1;
    args.expected.str = encoded_wapp_hash;
    args.manifest_digest = manifest_digest;

    suit_callback_ret_t ret = {};
    suit_err_t result = __wrap_suit_condition_callback(args, &ret);
    assert(result == SUIT_ERR_CONDITION_MISMATCH);
    assert(ret.reason == SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED);
}

static void test_image_match_ok(void)
{
    // Matching size and digest should succeed.

    //echo "['wapp-bin']" | diag2cbor.rb | xxd -p
    const uint8_t wapp_bin[] = {0x81, 0x48, 0x77, 0x61, 0x70, 0x70, 0x2d, 0x62, 0x69, 0x6e};
    UsefulBufC wapp_bin_buf = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(wapp_bin);

    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    suit_store_args_t store_args = {};

    tc_manager_test_helper_store_payload(wapp_bin_buf, manifest_digest, &store_args);
    assert(tc_manager_store_record_from_store_args(&store_args) == 0);

    uint8_t wapp_hash_bytes[SHA256_DIGEST_LENGTH] = {0};
    uint8_t encoded_buf[128] = {0};
    UsefulBufC encoded_wapp_hash = tc_manager_test_helper_calculate_sha256_and_encode_suit_digest(
                                                        wapp_bin,
                                                        sizeof(wapp_bin),
                                                        wapp_hash_bytes,
                                                        sizeof(wapp_hash_bytes),
                                                        encoded_buf,
                                                        sizeof(encoded_buf));

    suit_condition_args_t args = {};
    args.condition = SUIT_CONDITION_IMAGE_MATCH;
    args.expected.u64 = sizeof(wapp_bin);
    args.expected.str = encoded_wapp_hash;
    args.manifest_digest = manifest_digest;

    suit_callback_ret_t ret = {};
    suit_err_t result = __wrap_suit_condition_callback(args, &ret);
    assert(result == SUIT_SUCCESS);
}

static void test_image_match_size_mismatch(void)
{
    // Size mismatch should fail with condition failed.

    //echo "['wapp-bin']" | diag2cbor.rb | xxd -p
    const uint8_t wapp_bin[] = {0x81, 0x48, 0x77, 0x61, 0x70, 0x70, 0x2d, 0x62, 0x69, 0x6e};
    UsefulBufC wapp_bin_buf = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(wapp_bin);

    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    suit_store_args_t store_args = {};

    tc_manager_test_helper_store_payload(wapp_bin_buf, manifest_digest, &store_args);
    assert(tc_manager_store_record_from_store_args(&store_args) == 0);


    uint8_t wapp_hash_bytes[SHA256_DIGEST_LENGTH] = {0};
    uint8_t encoded_buf[128] = {0};
    UsefulBufC encoded_wapp_hash = tc_manager_test_helper_calculate_sha256_and_encode_suit_digest(
                                                        wapp_bin,
                                                        sizeof(wapp_bin),
                                                        wapp_hash_bytes,
                                                        sizeof(wapp_hash_bytes),
                                                        encoded_buf,
                                                        sizeof(encoded_buf));

    suit_condition_args_t args = {};
    args.condition = SUIT_CONDITION_IMAGE_MATCH;
    args.expected.u64 = encoded_wapp_hash.len + 1;
    args.expected.str = encoded_wapp_hash;
    args.manifest_digest = manifest_digest;

    suit_callback_ret_t ret = {};
    suit_err_t result = __wrap_suit_condition_callback(args, &ret);
    assert(result == SUIT_ERR_CONDITION_MISMATCH);
    assert(ret.reason == SUIT_REPORT_REASON_CONDITION_FAILED);
}

static void test_image_match_digest_mismatch(void)
{
    // Digest mismatch should fail with condition failed.
    
    //echo "['wapp-bin']" | diag2cbor.rb | xxd -p
    const uint8_t wapp_bin[] = {0x81, 0x48, 0x77, 0x61, 0x70, 0x70, 0x2d, 0x62, 0x69, 0x6e};
    UsefulBufC wapp_bin_buf = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(wapp_bin);

    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    suit_store_args_t store_args = {};

    tc_manager_test_helper_store_payload(wapp_bin_buf, manifest_digest, &store_args);
    assert(tc_manager_store_record_from_store_args(&store_args) == 0);


    const uint8_t other_wapp_bin[] = {0x81, 0x4e, 0x6f, 0x74, 0x68, 0x65, 0x72, 0x2d, 0x77, 0x61, 0x70, 0x70, 0x2d, 0x62, 0x69, 0x6e};
    uint8_t wapp_hash_bytes[SHA256_DIGEST_LENGTH] = {0};
    uint8_t encoded_buf[128] = {0};
    UsefulBufC encoded_wapp_hash = tc_manager_test_helper_calculate_sha256_and_encode_suit_digest(
                                                        other_wapp_bin,
                                                        sizeof(other_wapp_bin),
                                                        wapp_hash_bytes,
                                                        sizeof(wapp_hash_bytes),
                                                        encoded_buf,
                                                        sizeof(encoded_buf));

    suit_condition_args_t args = {};
    args.condition = SUIT_CONDITION_IMAGE_MATCH;
    args.expected.u64 = sizeof(wapp_bin);
    args.expected.str = encoded_wapp_hash;
    args.manifest_digest = manifest_digest;

    suit_callback_ret_t ret = {};
    suit_err_t result = __wrap_suit_condition_callback(args, &ret);
    assert(result == SUIT_ERR_CONDITION_MISMATCH);
    assert(ret.reason == SUIT_REPORT_REASON_CONDITION_FAILED);
}

static void test_image_match_wrong_algorithm(void)
{
    // Unsupported digest algorithm should return not implemented.

    //echo "['wapp-bin']" | diag2cbor.rb | xxd -p
    const uint8_t wapp_bin[] = {0x81, 0x48, 0x77, 0x61, 0x70, 0x70, 0x2d, 0x62, 0x69, 0x6e};
    UsefulBufC wapp_bin_buf = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(wapp_bin);

    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    suit_store_args_t store_args = {};

    tc_manager_test_helper_store_payload(wapp_bin_buf, manifest_digest, &store_args);
    assert(tc_manager_store_record_from_store_args(&store_args) == 0);

    uint8_t wapp_hash_bytes[SHA256_DIGEST_LENGTH] = {0};
    UsefulBufC payload = {wapp_bin, sizeof(wapp_bin)};
    UsefulBuf digest_out = {wapp_hash_bytes, sizeof(wapp_hash_bytes)};
    suit_err_t digest_ret = suit_generate_sha256(payload, digest_out);
    assert(digest_ret == SUIT_SUCCESS);
    uint8_t encoded_buf[128] = {0};
    UsefulBufC encoded_wapp_hash = tc_manager_test_helper_encode_suit_digest(
                                                        SUIT_ALGORITHM_ID_SHA384,
                                                        UsefulBuf_Const(digest_out),
                                                        encoded_buf,
                                                        sizeof(encoded_buf));

    suit_condition_args_t args = {};
    args.condition = SUIT_CONDITION_IMAGE_MATCH;
    args.expected.u64 = sizeof(wapp_bin);
    args.expected.str = encoded_wapp_hash;
    args.manifest_digest = manifest_digest;

    suit_callback_ret_t ret = {};
    suit_err_t result = __wrap_suit_condition_callback(args, &ret);
    assert(result == SUIT_ERR_NOT_IMPLEMENTED); // unsupported algorithm, only SHA256 is supported
    assert(ret.reason == SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED);
}


int main(void)
{
    test_image_match_without_store_app();
    printf("[PASS] 1/5 image-match without stored app payload (expect unsupported)\n");
    test_image_match_ok();
    printf("[PASS] 2/5 image-match with correct size and digest\n");
    test_image_match_size_mismatch();
    printf("[PASS] 3/5 image-match size mismatch (expect condition failed)\n");
    test_image_match_digest_mismatch();
    printf("[PASS] 4/5 image-match digest mismatch (expect condition failed)\n");
    test_image_match_wrong_algorithm();
    printf("[PASS] 5/5 image-match with unsupported digest algorithm\n");

    return 0;
}
