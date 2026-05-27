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
#include "tc_manager.h"
#include "generate_keypair.h"
#include "test_helpers.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "qcbor/UsefulBuf.h"
#include "qcbor/qcbor_encode.h"
#include "teep/teep_message_data.h"
#include "csuit/suit_manifest_process.h"
#ifdef __cplusplus
}
#undef delete
#endif

teep_err_t process_query_request(const teep_query_request_t *query_request,
                                 UsefulBuf msg_buf,
                                 const char *app_name,
                                 teep_key_t *key_pair,
                                 teep_message_t *message);

teep_mechanism_t agent_sign = {};
teep_mechanism_t tam_verify = {};
extern "C" {
teep_key_state_t g_key_state = TEEP_KEY_READY;
}

teep_err_t create_evidence_generic(const teep_query_request_t *query_request,
                                   UsefulBuf buf,
                                   teep_key_t *key_pair,
                                   UsefulBufC *ret)
{
    (void)query_request;
    (void)buf;
    (void)key_pair;
    (void)ret;
    return TEEP_ERR_UNEXPECTED_ERROR;
}

teep_err_t create_evidence_dcap_envelope(const teep_query_request_t *query_request,
                                         UsefulBuf buf,
                                         teep_key_t *key_pair,
                                         UsefulBufC *ret)
{
    (void)query_request;
    (void)buf;
    (void)key_pair;
    (void)ret;
    return TEEP_ERR_UNEXPECTED_ERROR;
}

static UsefulBufC test_helper_build_expected_tc_info(const char *wapp_name,
                                                     const char *wapp_bin_text,
                                                     uint8_t *buf,
                                                     size_t buf_size)
{
    assert(wapp_name != NULL);
    assert(wapp_bin_text != NULL);
    assert(buf != NULL);
    assert(buf_size > 0);

    uint8_t wapp_hash[SHA256_DIGEST_LENGTH] = {0};
    UsefulBufC wapp_bin = { wapp_bin_text, strlen(wapp_bin_text) };
    assert(suit_generate_sha256(wapp_bin, UsefulBuf_FROM_BYTE_ARRAY(wapp_hash)) == SUIT_SUCCESS);

    QCBOREncodeContext context;
    UsefulBuf out = { buf, buf_size };
    UsefulBufC encoded = NULLUsefulBufC;
    UsefulBufC component_name = { wapp_name, strlen(wapp_name) };

    QCBOREncode_Init(&context, out);
    QCBOREncode_OpenMap(&context);
    QCBOREncode_OpenArrayInMapN(&context, 0);
    QCBOREncode_AddBytes(&context, component_name);
    QCBOREncode_CloseArray(&context);
    QCBOREncode_BstrWrapInMapN(&context, 3);
    QCBOREncode_OpenArray(&context);
    QCBOREncode_AddInt64(&context, -16);
    QCBOREncode_AddBytes(&context, UsefulBuf_FROM_BYTE_ARRAY(wapp_hash));
    QCBOREncode_CloseArray(&context);
    QCBOREncode_CloseBstrWrap(&context, NULL);
    QCBOREncode_CloseMap(&context);
    assert(QCBOREncode_Finish(&context, &encoded) == QCBOR_SUCCESS);
    return encoded;
}

static void test_process_query_request_builds_requested_and_tc_list(void)
{
    tc_manager_remove_all();

    uint8_t manifest_digest_a_bytes[] = {0xA1};
    uint8_t manifest_digest_b_bytes[] = {0xA2};
    UsefulBufC manifest_digest_a = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_a_bytes);
    UsefulBufC manifest_digest_b = UsefulBuf_FROM_BYTE_ARRAY_LITERAL(manifest_digest_b_bytes);

    assert(tc_manager_test_helper_store_complete_record(
               manifest_digest_a, 0, "manifest-a.suit", "app-a", "wapp-bin-a") == 0);
    assert(tc_manager_test_helper_store_complete_record(
               manifest_digest_b, 1, "manifest-b.suit", "app-b", "wapp-bin-b") == 0);

    teep_query_request_t query_request = {};
    query_request.type = TEEP_TYPE_QUERY_REQUEST;
    query_request.data_item_requested.trusted_components = true;

    uint8_t msg_buf_storage[4096] = {0};
    UsefulBuf msg_buf = { msg_buf_storage, sizeof(msg_buf_storage) };
    teep_key_t key_pair = {};
    teep_message_t message = {};

    const char *app_name = "request-app.wasm";
    assert(process_query_request(&query_request, msg_buf, app_name, &key_pair, &message) == TEEP_SUCCESS);

    const teep_query_response_t *query_response = (const teep_query_response_t *)&message;
    assert(query_response->type == TEEP_TYPE_QUERY_RESPONSE);
    assert((query_response->contains & TEEP_MESSAGE_CONTAINS_REQUESTED_TC_LIST) != 0);
    assert((query_response->contains & TEEP_MESSAGE_CONTAINS_TC_LIST) != 0);
    assert(query_response->requested_tc_list.len == 1);
    assert(query_response->tc_list.len == 2);

    uint8_t expected_requested_component_id_buf[128] = {0};
    UsefulBufC expected_requested_component_id =
        tc_manager_test_helper_encode_component_id(app_name,
                                                   expected_requested_component_id_buf,
                                                   sizeof(expected_requested_component_id_buf));
    UsefulBufC actual_requested_component_id = {
        query_response->requested_tc_list.items[0].component_id.ptr,
        query_response->requested_tc_list.items[0].component_id.len
    };
    assert(UsefulBuf_Compare(actual_requested_component_id, expected_requested_component_id) == 0);

    uint8_t expected_tc_info_a_buf[256] = {0};
    uint8_t expected_tc_info_b_buf[256] = {0};
    UsefulBufC expected_tc_info_a =
        test_helper_build_expected_tc_info("app-a",
                                           "wapp-bin-a",
                                           expected_tc_info_a_buf,
                                           sizeof(expected_tc_info_a_buf));
    UsefulBufC expected_tc_info_b =
        test_helper_build_expected_tc_info("app-b",
                                           "wapp-bin-b",
                                           expected_tc_info_b_buf,
                                           sizeof(expected_tc_info_b_buf));

    UsefulBufC actual_tc_info_0 = {
        query_response->tc_list.items[0].ptr,
        query_response->tc_list.items[0].len
    };
    UsefulBufC actual_tc_info_1 = {
        query_response->tc_list.items[1].ptr,
        query_response->tc_list.items[1].len
    };

    bool match_order_a_b =
        UsefulBuf_Compare(actual_tc_info_0, expected_tc_info_a) == 0 &&
        UsefulBuf_Compare(actual_tc_info_1, expected_tc_info_b) == 0;
    bool match_order_b_a =
        UsefulBuf_Compare(actual_tc_info_0, expected_tc_info_b) == 0 &&
        UsefulBuf_Compare(actual_tc_info_1, expected_tc_info_a) == 0;
    assert(match_order_a_b || match_order_b_a);

    for (size_t i = 0; i < query_response->tc_list.len; i++) {
        free((void *)query_response->tc_list.items[i].ptr);
    }
    tc_manager_remove_all();
}

static void test_process_query_request_returns_error_when_app_name_is_invalid(void)
{
    tc_manager_remove_all();

    teep_query_request_t query_request = {};
    query_request.type = TEEP_TYPE_QUERY_REQUEST;
    query_request.data_item_requested.trusted_components = true;

    uint8_t msg_buf_storage[1024] = {0};
    UsefulBuf msg_buf = { msg_buf_storage, sizeof(msg_buf_storage) };
    teep_key_t key_pair = {};
    teep_message_t message = {};

    assert(process_query_request(&query_request, msg_buf, NULL, &key_pair, &message) == TEEP_SUCCESS);
    const teep_error_t *teep_error = (const teep_error_t *)&message;
    assert(teep_error->type == TEEP_TYPE_ERROR);
}

int main(void)
{
    test_process_query_request_builds_requested_and_tc_list();
    printf("[PASS] 1/2 process_query_request builds requested_tc_list and tc_list\n");

    test_process_query_request_returns_error_when_app_name_is_invalid();
    printf("[PASS] 2/2 process_query_request rejects invalid app_name\n");
    return 0;
}
