/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_manager.h"
#include "test_helpers.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "qcbor/UsefulBuf.h"
#include "csuit/suit_manifest_process.h"
#ifdef __cplusplus
}
#undef delete
#endif

static void test_tc_manager_get_tc_list_returns_expected_items_for_multiple_complete_records(void)
{
    tc_manager_remove_all();

    uint8_t manifest_digest_a_bytes[] = {0xA1};
    uint8_t manifest_digest_b_bytes[] = {0xA2};
    UsefulBufC manifest_digest_a = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_a_bytes);
    UsefulBufC manifest_digest_b = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_b_bytes);

    uint64_t manifest_sequence_number_a = 0;
    uint64_t manifest_sequence_number_b = 1;

    assert(tc_manager_test_helper_store_complete_record(
               manifest_digest_a, manifest_sequence_number_a, "manifest-a.suit", "app-a", "wapp-binary") == 0);
    assert(tc_manager_test_helper_store_complete_record(
               manifest_digest_b, manifest_sequence_number_b, "manifest-b.suit", "app-b", "wapp-binary") == 0);

    uint8_t expected_wapp_hash[SHA256_DIGEST_LENGTH] = {0};
    UsefulBufC wapp_bin = UsefulBuf_FROM_SZ_LITERAL("wapp-binary");
    assert(suit_generate_sha256(wapp_bin,
                                UsefulBuf_FROM_BYTE_ARRAY(expected_wapp_hash)) == SUIT_SUCCESS);

    size_t capacity = tc_manager_record_count();
    assert(capacity == 2);
    tc_list_item_t *items = (tc_list_item_t *)calloc(capacity, sizeof(tc_list_item_t));
    assert(items != NULL);
    size_t out_count = 0;
    assert(tc_manager_get_tc_list(items, capacity, &out_count) == 0);
    assert(out_count == 2);
    for (size_t i = 0; i < out_count; i++) {
        if (strcmp(items[i].component_id, "app-a") == 0) {
            assert(memcmp(items[i].tc_image_digest,
                          expected_wapp_hash,
                          SHA256_DIGEST_LENGTH) == 0);
            continue;
        }
        if (strcmp(items[i].component_id, "app-b") == 0) {
            assert(memcmp(items[i].tc_image_digest,
                          expected_wapp_hash,
                          SHA256_DIGEST_LENGTH) == 0);
            continue;
        }
        assert(!"unexpected component_id in tc_list");
    }
    free(items);
}

static void test_tc_manager_get_tc_list_returns_zero_count_when_record_is_empty(void)
{
    tc_manager_remove_all();

    tc_list_item_t items[1] = {};
    size_t out_count = 99;
    assert(tc_manager_get_tc_list(items, 1, &out_count) == 0);
    assert(out_count == 0);
}

static void test_tc_manager_get_tc_list_returns_error_when_capacity_is_zero(void)
{
    tc_manager_remove_all();

    tc_list_item_t items[1] = {};
    size_t out_count = 99;
    assert(tc_manager_get_tc_list(items, 0, &out_count) == -1);
    assert(out_count == 0);
}

static void test_tc_manager_get_tc_list_returns_error_when_capacity_is_insufficient(void)
{
    tc_manager_remove_all();

    uint8_t manifest_digest_a_bytes[] = {0xA1};
    uint8_t manifest_digest_b_bytes[] = {0xA2};
    UsefulBufC manifest_digest_a = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_a_bytes);
    UsefulBufC manifest_digest_b = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_b_bytes);

    uint64_t manifest_sequence_number_a = 0;
    uint64_t manifest_sequence_number_b = 1;

    assert(tc_manager_test_helper_store_complete_record(
               manifest_digest_a, manifest_sequence_number_a, "manifest-a.suit", "app-a", "wapp-binary") == 0);
    assert(tc_manager_test_helper_store_complete_record(
               manifest_digest_b, manifest_sequence_number_b, "manifest-b.suit", "app-b", "wapp-binary") == 0);

    size_t record_count = tc_manager_record_count();
    assert(record_count == 2);
    size_t insufficient_capacity = record_count - 1;
    tc_list_item_t *items = (tc_list_item_t *)calloc(insufficient_capacity, sizeof(tc_list_item_t));
    assert(items != NULL);
    size_t out_count = 99;
    assert(tc_manager_get_tc_list(items, insufficient_capacity, &out_count) == -1);
    assert(out_count == 0);
    free(items);
}

static void test_tc_manager_get_tc_list_returns_error_when_out_items_is_null(void)
{
    tc_manager_remove_all();

    size_t out_count = 99;
    assert(tc_manager_get_tc_list(NULL, 1, &out_count) == -1);
    assert(out_count == 0);
}

static void test_tc_manager_get_tc_list_returns_error_when_out_count_is_null(void)
{
    tc_manager_remove_all();

    tc_list_item_t items[1] = {};
    assert(tc_manager_get_tc_list(items, 1, NULL) == -1);
}

int main(void)
{
    test_tc_manager_get_tc_list_returns_expected_items_for_multiple_complete_records();
    printf("[PASS] 1/6 get_tc_list returns expected items for complete records\n");
    test_tc_manager_get_tc_list_returns_zero_count_when_record_is_empty();
    printf("[PASS] 2/6 get_tc_list returns zero count when records are empty\n");
    test_tc_manager_get_tc_list_returns_error_when_capacity_is_zero();
    printf("[PASS] 3/6 get_tc_list rejects capacity=0\n");
    test_tc_manager_get_tc_list_returns_error_when_capacity_is_insufficient();
    printf("[PASS] 4/6 get_tc_list rejects insufficient capacity\n");
    test_tc_manager_get_tc_list_returns_error_when_out_items_is_null();
    printf("[PASS] 5/6 get_tc_list rejects null out_items\n");
    test_tc_manager_get_tc_list_returns_error_when_out_count_is_null();
    printf("[PASS] 6/6 get_tc_list rejects null out_count\n");
    return 0;
}
