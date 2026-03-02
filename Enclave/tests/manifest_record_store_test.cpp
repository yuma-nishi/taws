/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tc_manager.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "qcbor/UsefulBuf.h"
#include "qcbor/qcbor_encode.h"
#include "csuit/suit_manifest_process.h"
#ifdef __cplusplus
}
#undef delete
#endif


static void tc_manager_test_helper_store_manifest(UsefulBufC manifest_digest_bytes,
                           uint64_t manifest_sequence_number,
                           UsefulBufC manifest_component_id,
                        suit_store_args_t *args)
{
    args->operation = SUIT_STORE;
    args->manifest_digest = manifest_digest_bytes;
    args->manifest_sequence_number = manifest_sequence_number;
    args->is_manifest_itself = true;
    args->dst = manifest_component_id;
    args->src_buf.ptr = (uint8_t *)"manifest-binary";
    args->src_buf.len = 8;
}

static void tc_manager_test_helper_store_app(UsefulBufC manigest_digest_bytes,
                      UsefulBufC wapp_name,
                    suit_store_args_t *args)
{
    args->operation = SUIT_STORE;
    args->manifest_digest = manigest_digest_bytes;
    args->is_manifest_itself = false;
    args->dst = wapp_name;
    args->src_buf.ptr = (uint8_t *)"wapp-binary";
    args->src_buf.len = 11;
}

static void test_manifest_only_is_incomplete(void)
{
    tc_manager_remove_all();
    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    suit_store_args_t args = {};

    //echo "['manifet']" | diag2cbor.rb | xxd -p
    uint8_t manifest_name_buf[] = {0x81, 0x47, 0x6d, 0x61, 0x6e, 0x69, 0x66, 0x65, 0x74};
    UsefulBufC manifest_component_id = {manifest_name_buf, sizeof(manifest_name_buf)};
    uint64_t manifest_sequence_number = 1;

    tc_manager_test_helper_store_manifest(manifest_digest, manifest_sequence_number, manifest_component_id, &args);
    assert(tc_manager_store_record_from_store_args(&args) == 0);

    tc_manager_check_and_update_record(manifest_digest);

    const manifest_record_t *record = tc_manager_find_record_by_digest(manifest_digest);
    assert(record == NULL);
}

static void test_app_only_is_incomplete(void)
{
    tc_manager_remove_all();
    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    //echo "['app']" | diag2cbor.rb | xxd -p
    uint8_t app_name_buf[] = {0x81, 0x43, 0x61, 0x70, 0x70};
    UsefulBufC app_component_id = {app_name_buf, sizeof(app_name_buf)};

    suit_store_args_t args = {};
    tc_manager_test_helper_store_app(manifest_digest, app_component_id, &args);
    assert(tc_manager_store_record_from_store_args(&args) == 0);

    tc_manager_check_and_update_record(manifest_digest);

    const manifest_record_t *record = tc_manager_find_record_by_digest(manifest_digest);
    assert(record == NULL);
}

static void test_manifest_and_app_is_complete(void)
{
    tc_manager_remove_all();
    uint8_t manifest_digest_bytes[] = {0xAA};
    UsefulBufC manifest_digest = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes);

    //echo "['manifet']" | diag2cbor.rb | xxd -p
    uint8_t manifest_name_buf[] = {0x81, 0x47, 0x6d, 0x61, 0x6e, 0x69, 0x66, 0x65, 0x74};
    UsefulBufC manifest_component_id = {manifest_name_buf, sizeof(manifest_name_buf)};
    uint64_t manifest_sequence_number = 1;
    suit_store_args_t args1 = {};

    tc_manager_test_helper_store_manifest(manifest_digest, manifest_sequence_number, manifest_component_id, &args1);
    assert(tc_manager_store_record_from_store_args(&args1) == 0);

     //echo "['app']" | diag2cbor.rb | xxd -p
    uint8_t app_name_buf[] = {0x81, 0x43, 0x61, 0x70, 0x70};
    UsefulBufC app_component_id = {app_name_buf, sizeof(app_name_buf)};
    suit_store_args_t args2 = {};

    tc_manager_test_helper_store_app(manifest_digest, app_component_id, &args2);
    assert(tc_manager_store_record_from_store_args(&args2) == 0);

    const manifest_record_t *record = tc_manager_find_record_by_digest(manifest_digest);

    assert(record != NULL);
    assert(record->manifest_digest.len == 1);
    assert(memcmp(record->manifest_digest.ptr, manifest_digest_bytes, 1) == 0);
    assert(strcmp(record->manifest_name, "manifet") == 0);
    assert(record->manifest_sequence_number == manifest_sequence_number);
    assert(record->manifest_bin.len == 8);
    assert(memcmp(record->manifest_bin.ptr, "manifest-binary", 8) == 0);
    assert(strcmp(record->wapp_name, "app") == 0);
    {
        uint8_t expected_wapp_hash[SHA256_DIGEST_LENGTH] = {0};
        UsefulBuf payload = {(uint8_t *)"wapp-binary", 11};
        suit_generate_sha256(UsefulBuf_Const(payload),
                             UsefulBuf_FROM_BYTE_ARRAY(expected_wapp_hash));
        assert(memcmp(record->wapp_hash, expected_wapp_hash, SHA256_DIGEST_LENGTH) == 0);
    }
    assert(record->wapp_bin.len == 11);
    assert(memcmp(record->wapp_bin.ptr, "wapp-binary", 11) == 0);
}

static void test_update_or_discard(void)
{
    tc_manager_remove_all();
    uint8_t manifest_digest_bytes_a[] = {0x10};
    uint8_t manifest_digest_bytes_b[] = {0x11};

    UsefulBufC manifest_digest_a = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes_a);
    UsefulBufC manifest_digest_b = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_bytes_b);
    uint64_t manifest_sequence_number_a = 1;
    uint64_t manifest_sequence_number_b = 2;
    //echo "['manifet']" | diag2cbor.rb | xxd -p
    uint8_t manifest_name_buf[] = {0x81, 0x47, 0x6d, 0x61, 0x6e, 0x69, 0x66, 0x65, 0x74};
    UsefulBufC manifest_component_id = {manifest_name_buf, sizeof(manifest_name_buf)};

    //echo "['app']" | diag2cbor.rb | xxd -p
    uint8_t app_name_buf[] = {0x81, 0x43, 0x61, 0x70, 0x70};
    UsefulBufC app_component_id = {app_name_buf, sizeof(app_name_buf)};

    // Store manifest+app for manifest_a (older).
    suit_store_args_t args = {};
    tc_manager_test_helper_store_manifest(manifest_digest_a, manifest_sequence_number_a, manifest_component_id, &args);
    args.src_buf.ptr = (uint8_t *)"manifest-A";
    args.src_buf.len = 10;
    assert(tc_manager_store_record_from_store_args(&args) == 0);
    tc_manager_test_helper_store_app(manifest_digest_a, app_component_id, &args);
    args.src_buf.ptr = (uint8_t *)"wapp-A";
    args.src_buf.len = 6;
    assert(tc_manager_store_record_from_store_args(&args) == 0);

    // Store manifest+app for manifest_b (newer) with same app component_id.
    tc_manager_test_helper_store_manifest(manifest_digest_b, manifest_sequence_number_b, manifest_component_id, &args);
    args.src_buf.ptr = (uint8_t *)"manifest-B";
    args.src_buf.len = 10;
    assert(tc_manager_store_record_from_store_args(&args) == 0);
    tc_manager_test_helper_store_app(manifest_digest_b, app_component_id, &args);
    args.src_buf.ptr = (uint8_t *)"wapp-B";
    args.src_buf.len = 6;
    assert(tc_manager_store_record_from_store_args(&args) == 0);

    tc_manager_check_and_update_record(manifest_digest_b);

    assert(tc_manager_record_count() == 1);
    const manifest_record_t *record = tc_manager_find_record_by_wappname("app");
    assert(record != NULL);
    assert(record->manifest_sequence_number == 2);
    assert(memcmp(record->manifest_digest.ptr,
                  manifest_digest_bytes_b,
                  sizeof(manifest_digest_bytes_b)) == 0);
    assert(strcmp(record->wapp_name, "app") == 0);
    assert(record->manifest_bin.len == 10);
    assert(memcmp(record->manifest_bin.ptr, "manifest-B", 10) == 0);
    {
        uint8_t expected_wapp_hash[SHA256_DIGEST_LENGTH] = {0};
        UsefulBuf payload = {(uint8_t *)"wapp-B", 6};
        suit_generate_sha256(UsefulBuf_Const(payload),
                             UsefulBuf_FROM_BYTE_ARRAY(expected_wapp_hash));
        assert(memcmp(record->wapp_hash, expected_wapp_hash, SHA256_DIGEST_LENGTH) == 0);
    }
    assert(record->wapp_bin.len == 6);
    assert(memcmp(record->wapp_bin.ptr, "wapp-B", 6) == 0);
}

int main(void)
{
    test_manifest_only_is_incomplete();
    printf("[PASS] 1/4 manifest-only record is incomplete\n");
    test_app_only_is_incomplete();
    printf("[PASS] 2/4 app-only record is incomplete\n");
    test_manifest_and_app_is_complete();
    printf("[PASS] 3/4 manifest+app record is complete\n");
    test_update_or_discard();
    printf("[PASS] 4/4 update or discard duplicate app record\n");
    return 0;
}
