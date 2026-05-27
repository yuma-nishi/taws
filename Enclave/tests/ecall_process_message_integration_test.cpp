/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Enclave.h"
#include "ecall_process_teep_result.h"
#include "tc_manager.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "qcbor/UsefulBuf.h"
#include "teep/teep_common.h"
#ifdef __cplusplus
}
#undef delete
#endif

extern "C" teep_err_t ecall_teep_set_esp256_key(void);
extern "C" void ecall_teep_free_keypair(void);
extern "C" ecall_process_teep_result_t ecall_process_message(const uint8_t *recv_buf,
                                                             size_t recv_len,
                                                             const char *app_name,
                                                             uint8_t *send_buf,
                                                             size_t allocated_len,
                                                             size_t *actual_len);

static bool read_file(const char *path, uint8_t **out_buf, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(fp);
        return false;
    }
    size_t read_len = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read_len != (size_t)size) {
        free(buf);
        return false;
    }
    *out_buf = buf;
    *out_len = (size_t)size;
    return true;
}

typedef struct test_record_snapshot_for_assertion {
    uint8_t manifest_digest_bytes[64];
    size_t manifest_digest_len;
    uint64_t manifest_sequence_number;
} test_record_snapshot_for_assertion_t;

static void test_helper_copy_record_snapshot(const manifest_record_t *record,
                                             test_record_snapshot_for_assertion_t *out_snapshot)
{
    assert(record != NULL);
    assert(out_snapshot != NULL);
    assert(record->manifest_digest.len <= sizeof(out_snapshot->manifest_digest_bytes));
    memcpy(out_snapshot->manifest_digest_bytes,
           record->manifest_digest.ptr,
           record->manifest_digest.len);
    out_snapshot->manifest_digest_len = record->manifest_digest.len;
    out_snapshot->manifest_sequence_number = record->manifest_sequence_number;
}

int main(void)
{
    const char *k_wapp_name = "app.wasm";
    const char *k_query_request_path = "tam_mock_server/query_request.tam.esp256.cose";
    const char *k_update0_path = "tam_mock_server/update0.tam.esp256.cose";
    const char *k_update1_path = "tam_mock_server/update1.tam.esp256.cose";

    uint8_t *query_request_buf = NULL;
    uint8_t *update_message_v0_buf = NULL;
    uint8_t *update_message_v1_buf = NULL;
    size_t query_request_len = 0;
    size_t update_message_v0_len = 0;
    size_t update_message_v1_len = 0;
    uint8_t send_buf[4096] = {0};
    size_t actual_len = 0;

    test_record_snapshot_for_assertion_t snapshot_after_update0 = {};

    tc_manager_remove_all();
    assert(ecall_teep_set_esp256_key() == TEEP_SUCCESS);

    assert(read_file(k_query_request_path, &query_request_buf, &query_request_len));
    assert(read_file(k_update0_path, &update_message_v0_buf, &update_message_v0_len));
    assert(read_file(k_update1_path, &update_message_v1_buf, &update_message_v1_len));

    // Step 1: QueryRequest.
    assert(ecall_process_message(query_request_buf,
                                 query_request_len,
                                 k_wapp_name,
                                 send_buf,
                                 sizeof(send_buf),
                                 &actual_len) == 0);
    assert(actual_len > 0);

    // Step 2: update0 -> initial clean install.
    assert(ecall_process_message(update_message_v0_buf,
                                 update_message_v0_len,
                                 k_wapp_name,
                                 send_buf,
                                 sizeof(send_buf),
                                 &actual_len) == 0);
    assert(actual_len > 0);
    assert(tc_manager_record_count() == 1);
    const manifest_record_t *record_after_update0 = tc_manager_find_record_by_wappname(k_wapp_name);
    assert(record_after_update0 != NULL);
    assert(record_after_update0->manifest_bin.len > 0);
    assert(record_after_update0->wapp_bin.len > 0);
    test_helper_copy_record_snapshot(record_after_update0, &snapshot_after_update0);
    printf("[PASS] 1/2 ecall_process_message processes update0 and stores record\n");

    // Step 3: need QueryRequest again before next Update.
    assert(ecall_process_message(query_request_buf,
                                 query_request_len,
                                 k_wapp_name,
                                 send_buf,
                                 sizeof(send_buf),
                                 &actual_len) == 0);
    assert(actual_len > 0);

    // Step 4: update1 -> version update keeps one active record.
    assert(ecall_process_message(update_message_v1_buf,
                                 update_message_v1_len,
                                 k_wapp_name,
                                 send_buf,
                                 sizeof(send_buf),
                                 &actual_len) == 0);
    assert(actual_len > 0);
    assert(tc_manager_record_count() == 1);
    const manifest_record_t *record_after_update1 = tc_manager_find_record_by_wappname(k_wapp_name);
    assert(record_after_update1 != NULL);
    assert(record_after_update1->manifest_bin.len > 0);
    assert(record_after_update1->wapp_bin.len > 0);
    assert(record_after_update1->manifest_sequence_number >= snapshot_after_update0.manifest_sequence_number);
    printf("[PASS] 2/2 ecall_process_message processes update1 and keeps updated single record\n");

    free(query_request_buf);
    free(update_message_v0_buf);
    free(update_message_v1_buf);
    ecall_teep_free_keypair();
    tc_manager_remove_all();
    return 0;
}
