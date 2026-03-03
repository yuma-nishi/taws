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
#include "suit_manifest_prime256v1_cose_key_public.h"
#include "tam_es256_public_key.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif

#include "qcbor/UsefulBuf.h"
#include "csuit/suit_common.h"
#include "csuit/suit_manifest_process.h"
#include "csuit/suit_manifest_print.h"
#include "teep/teep_cose.h"
#include "teep/teep_message_data.h"

#ifdef __cplusplus
}
#undef delete
#endif

#include "suit_config.h"

extern "C" suit_err_t __wrap_suit_store_callback(suit_store_args_t store_args);
extern "C" suit_err_t __wrap_suit_condition_callback(suit_condition_args_t condition_args,
                                                     suit_callback_ret_t *condition_ret);
extern "C" suit_err_t __wrap_suit_report_callback(suit_report_args_t report_args);

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

static void test_helper_process_update_message(const uint8_t *update_message_buf,
                                               size_t update_message_buf_len)
{
    teep_mechanism_t mechanism = {};
    teep_err_t teep_result = teep_key_init_es256_public_key(tam_es256_public_key,
                                                           NULLUsefulBufC,
                                                           &mechanism.key);
    assert(teep_result == TEEP_SUCCESS);
    mechanism.cose_tag = CBOR_TAG_COSE_SIGN1;
    mechanism.use = true;

    UsefulBufC signed_update_message = (UsefulBufC){ .ptr = update_message_buf, .len = update_message_buf_len };
    UsefulBufC update_message_payload_bytes = NULLUsefulBufC;
    teep_result = teep_verify_cose_sign1(signed_update_message, &mechanism, &update_message_payload_bytes);
    assert(teep_result == TEEP_SUCCESS);

    teep_message_t update_message_payload = {};
    teep_result = teep_set_message_from_bytes((const uint8_t *)update_message_payload_bytes.ptr,
                                              update_message_payload_bytes.len,
                                              &update_message_payload);
    assert(teep_result == TEEP_SUCCESS);
    assert(update_message_payload.teep_message.type == TEEP_TYPE_UPDATE);
    teep_update_t *parsed_update = &update_message_payload.teep_update;
    assert((parsed_update->contains & TEEP_MESSAGE_CONTAINS_MANIFEST_LIST) != 0);
    assert(parsed_update->manifest_list.len > 0);

    teep_buf_t manifest_entry = parsed_update->manifest_list.items[0];
    UsefulBufC envelope_bytes = (UsefulBufC){ .ptr = manifest_entry.ptr, .len = manifest_entry.len };
    suit_processor_context_t *processor_context =
        (suit_processor_context_t *)calloc(1, sizeof(suit_processor_context_t) + SUIT_MAX_DATA_SIZE);
    assert(processor_context != NULL);

    UsefulBuf manifest_buf = NULLUsefulBuf;
    suit_err_t result = suit_processor_init(processor_context,
                                            SUIT_MAX_DATA_SIZE,
                                            NULL,
                                            false,
                                            &manifest_buf);
    assert(result == SUIT_SUCCESS);

    if (envelope_bytes.ptr == NULL || envelope_bytes.len == 0 || envelope_bytes.len > manifest_buf.len) {
        printf("process_update : Failed to read Manifest.\n");
        assert(false);
    }

    suit_key_t recipient_key = {};
    result = suit_set_suit_key_from_cose_key(suit_manifest_esp256_cose_key_public, &recipient_key);
    assert(result == SUIT_SUCCESS);
    result = suit_processor_add_recipient_key(processor_context,
                                              (int)CBOR_TAG_COSE_SIGN1,
                                              T_COSE_ALGORITHM_ESP256,
                                              &recipient_key);
    assert(result == SUIT_SUCCESS);

    suit_process_flag_t process_flags = {0};
    process_flags.all = UINT16_MAX;
    process_flags.uninstall = 0;

    assert(envelope_bytes.len <= manifest_buf.len);
    memcpy(manifest_buf.ptr, envelope_bytes.ptr, envelope_bytes.len);
    manifest_buf.len = envelope_bytes.len;
    UsefulBufC envelope = UsefulBuf_Const(manifest_buf);
    result = suit_processor_add_manifest(processor_context, envelope, process_flags);
    if (result != SUIT_SUCCESS) {
        const char *err_str = suit_err_to_str(result);
        printf("suit_processor_add_manifest failed: %d%s%s\n",
               result,
               err_str != NULL ? " " : "",
               err_str != NULL ? err_str : "");
    }
    assert(result == SUIT_SUCCESS);
    result = suit_process_envelope(processor_context);
    if (result != SUIT_SUCCESS) {
        const char *err_str = suit_err_to_str(result);
        printf("suit_process_envelope failed: %d%s%s\n",
               result,
               err_str != NULL ? " " : "",
               err_str != NULL ? err_str : "");
    }
    assert(result == SUIT_SUCCESS);
    assert(tc_manager_check_and_update_record(processor_context->b.manifest_digest) == 0);

    suit_processor_free(processor_context);
    free(processor_context);
    teep_free_key(&mechanism.key);
}

int main(void)
{
    tc_manager_remove_all();

    uint8_t *update_message_buf_v0 = NULL;
    uint8_t *update_message_buf_v1 = NULL;
    size_t update_message_buf_v0_len = 0;
    size_t update_message_buf_v1_len = 0;

    assert(read_file("../testvector/prebuilt/update0.tam.esp256.cose", &update_message_buf_v0, &update_message_buf_v0_len));
    assert(read_file("../testvector/prebuilt/update1.tam.esp256.cose", &update_message_buf_v1, &update_message_buf_v1_len));

    // update0: clean install stores one active record.
    test_helper_process_update_message(update_message_buf_v0, update_message_buf_v0_len);
    assert(tc_manager_record_count() == 1);
    const manifest_record_t *manifest_record = tc_manager_find_record_by_wappname("app.wasm");
    assert(manifest_record != NULL);
    assert(strcmp(manifest_record->wapp_name, "app.wasm") == 0);
    assert(manifest_record->wapp_bin.len == 65132);
    assert(manifest_record->manifest_sequence_number == 0);
    assert(!UsefulBuf_IsNULLOrEmpty(manifest_record->manifest_digest));
    assert(strcmp(manifest_record->manifest_name, "manifest.app.wasm.0.suit") == 0);
    assert(!UsefulBuf_IsNULLOrEmpty(manifest_record->manifest_bin));

    printf("[PASS] 1/2 update0: clean install stored expected manifest/app record\n");

    // update1: version upgrade keeps one active record and applies update rules.
    test_helper_process_update_message(update_message_buf_v1, update_message_buf_v1_len);
    assert(tc_manager_record_count() == 1);

    const manifest_record_t *updated_manifest_record = tc_manager_find_record_by_wappname("app.wasm");
    
    assert(updated_manifest_record != NULL);
    assert(strcmp(updated_manifest_record->wapp_name, "app.wasm") == 0);
    assert(updated_manifest_record->wapp_bin.len == 65148);
    assert(updated_manifest_record->manifest_sequence_number == 1);
    assert(!UsefulBuf_IsNULLOrEmpty(updated_manifest_record->manifest_digest));
    assert(strcmp(updated_manifest_record->manifest_name, "manifest.app.wasm.1.suit") == 0);
    assert(!UsefulBuf_IsNULLOrEmpty(updated_manifest_record->manifest_bin));
    
    printf("[PASS] 2/2 update1: fixed vector update stored expected upgraded record\n");

    free(update_message_buf_v0);
    free(update_message_buf_v1);
    return 0;
}
