/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdarg.h>
#include <stdio.h>      /* vsnprintf */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "Enclave.h"
#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif

#include "qcbor/UsefulBuf.h"
#include "qcbor/qcbor_decode.h"
#include "qcbor/qcbor_encode.h"
#include "teep/teep_common.h"
#include "teep/teep_message_print.h"
#include "teep/teep_message_data.h"
#include "csuit/suit_manifest_process.h"
#include "csuit/suit_manifest_print.h"
#include "csuit/suit_manifest_callbacks.h"

#ifdef __cplusplus
}
#undef delete
#endif


#include "Enclave.h"
#include "generate_keypair.h"
#include "debug_print.h"
#include "tc_manager.h"
#include "teep_create_evidence.h"
#include "suit_manifest_prime256v1_cose_key_public.h"
#include "suit_manifest_process.h"
#include "suit_report_esp256_cose_key_private.h"
#include "suit_config.h"
#include "ecall_process_teep_result.h"
#include "teep_buffer_sizes.h"


#define MAX_SEND_BUFFER_SIZE            TEEP_SEND_BUFFER_SIZE
#define ERR_MSG_BUF_LEN                 32
#define WORK_BUF_LEN                    TEEP_WORK_BUFFER_SIZE
#define SUPPORTED_VERSION               0
#define SUPPORTED_CIPHER_SUITES_LEN     1
#define REPORT_SIZE (1024)

#ifndef SGX_EVIDENCE
#define SGX_EVIDENCE (1)
#endif

#if SGX_EVIDENCE != 0 && SGX_EVIDENCE != 1
#error "SGX_EVIDENCE must be 0 or 1"
#endif


const teep_cipher_suite_t supported_teep_cipher_suites[SUPPORTED_CIPHER_SUITES_LEN] = {
    { { { CBOR_TAG_COSE_SIGN1, T_COSE_ALGORITHM_ESP256 }, { 0, 0 } } }
};

static const teep_cipher_suite_t TeepCipherSuiteInvalid = {{
    {CBOR_TAG_INVALID16, TEEP_COSE_NONE},
    {CBOR_TAG_INVALID16, TEEP_COSE_NONE},
}};

static const int64_t k_suit_parameter_component_id = 0;
static const int64_t k_suit_parameter_image_digest = 3;
static const int64_t k_suit_digest_algorithm_sha256 = -16;
static const char k_dcap_quote_attestation_payload_format[] = "application/sgx-quote3-teep-bundle";

static void free_query_response_tc_list_buffers(teep_query_response_t *query_response)
{
    if (query_response == NULL) {
        return;
    }

    for (size_t i = 0; i < query_response->tc_list.len; i++) {
        free(const_cast<uint8_t *>(query_response->tc_list.items[i].ptr));
        query_response->tc_list.items[i].ptr = NULL;
        query_response->tc_list.items[i].len = 0;
    }
    query_response->tc_list.len = 0;
}

static void encode_tc_info_map(QCBOREncodeContext *tc_info_context,
                               const tc_list_item_t *item,
                               size_t component_id_len)
{
    QCBOREncode_OpenMap(tc_info_context);
    QCBOREncode_OpenArrayInMapN(tc_info_context, k_suit_parameter_component_id);
    QCBOREncode_AddBytes(tc_info_context,
                         (UsefulBufC){ .ptr = item->component_id, .len = component_id_len });
    QCBOREncode_CloseArray(tc_info_context);
    QCBOREncode_BstrWrapInMapN(tc_info_context, k_suit_parameter_image_digest);
    QCBOREncode_OpenArray(tc_info_context);
    QCBOREncode_AddInt64(tc_info_context, k_suit_digest_algorithm_sha256);
    QCBOREncode_AddBytes(tc_info_context,
                         (UsefulBufC){ .ptr = item->tc_image_digest, .len = SHA256_DIGEST_LENGTH });
    QCBOREncode_CloseArray(tc_info_context);
    QCBOREncode_CloseBstrWrap(tc_info_context, NULL);
    QCBOREncode_CloseMap(tc_info_context);
}

static int build_tc_info_cbor_from_tc_list_item(const tc_list_item_t *item, teep_buf_t *out_tc_info)
{
    if (item == NULL || out_tc_info == NULL) {
        return -1;
    }

    size_t component_id_len = strnlen(item->component_id, TC_COMPONENT_ID_MAX_LEN + 1);
    if (component_id_len == 0 || component_id_len > TC_COMPONENT_ID_MAX_LEN) {
        return -1;
    }

    // 1st pass: calculate exact encoded size.
    size_t tc_info_buf_len = 0;
    QCBOREncodeContext tc_info_size_context;
    QCBOREncode_Init(&tc_info_size_context, SizeCalculateUsefulBuf);
    encode_tc_info_map(&tc_info_size_context, item, component_id_len);
    if (QCBOREncode_FinishGetSize(&tc_info_size_context, &tc_info_buf_len) != QCBOR_SUCCESS ||
        tc_info_buf_len == 0) {
        return -1;
    }

    // 2nd pass: encode into exactly-sized buffer.
    uint8_t *tc_info_buf = (uint8_t *)malloc(tc_info_buf_len);
    if (tc_info_buf == NULL) {
        return -1;
    }

    UsefulBuf tc_info_usefulbuf = { .ptr = tc_info_buf, .len = tc_info_buf_len };
    UsefulBufC encoded_tc_info = NULLUsefulBufC;
    QCBOREncodeContext tc_info_context;
    QCBOREncode_Init(&tc_info_context, tc_info_usefulbuf);
    encode_tc_info_map(&tc_info_context, item, component_id_len);
    if (QCBOREncode_Finish(&tc_info_context, &encoded_tc_info) != QCBOR_SUCCESS) {
        free(tc_info_buf);
        return -1;
    }

    out_tc_info->ptr = static_cast<const uint8_t *>(encoded_tc_info.ptr);
    out_tc_info->len = encoded_tc_info.len;
    return 0;
}

/*!
    \brief      Create teep-error message.

    \param[in]  token       Bstr token in sent message from the TAM.
    \param[in]  err_code    Integer err-code message set by caller.
    \param[in]  msg_buf Optional err-msg bytes set by caller.
    \param[in]  suit_report_buf Optional SUIT report bytes to include in teep-error.
    \param[out] message     Pointer of returned struct.

    \return     This returns only TEEP_SUCCESS;
 */
teep_err_t create_error(teep_buf_t token,
                        uint64_t err_code,
                        UsefulBuf msg_buf,
                        UsefulBufC suit_report_buf,
                        teep_message_t *message)
{
    teep_error_t *error = (teep_error_t *)message;
    error->type = TEEP_TYPE_ERROR;
    error->contains = 0;
    error->err_code = (teep_err_code_t)err_code;

    if (token.ptr != NULL && 8 <= token.len && token.len <= 64) {
        error->token = token;
        error->contains |= TEEP_MESSAGE_CONTAINS_TOKEN;
    }
    if (msg_buf.len > 0) {
        error->err_msg = (teep_buf_t){.len = msg_buf.len, .ptr = (uint8_t *)msg_buf.ptr};
        error->contains |= TEEP_MESSAGE_CONTAINS_ERR_MSG;
    }
    if (err_code == TEEP_ERR_CODE_MANIFEST_PROCESSING_FAILED &&
        suit_report_buf.ptr != NULL && suit_report_buf.len > 0) {
        error->suit_reports.len = 1;
        error->suit_reports.items[0] = (teep_buf_t){
            .len = suit_report_buf.len,
            .ptr = const_cast<uint8_t *>(
                static_cast<const uint8_t *>(suit_report_buf.ptr)),
        };
        error->contains |= TEEP_MESSAGE_CONTAINS_SUIT_REPORTS;
    }

    if (err_code == TEEP_ERR_CODE_PERMANENT_ERROR) {
        if (token.ptr == NULL || token.len < 8 || 64 < token.len) {
            /* the token is incorrect */
            error->err_code = TEEP_ERR_CODE_PERMANENT_ERROR;
        }
    }
    else if (err_code == TEEP_ERR_CODE_UNSUPPORTED_MSG_VERSION) {
        error->versions.len = 1;
        error->versions.items[0] = SUPPORTED_VERSION;
        error->contains |= TEEP_MESSAGE_CONTAINS_VERSIONS;
    }
    else if (err_code == TEEP_ERR_CODE_UNSUPPORTED_CIPHER_SUITES) {
        error->supported_teep_cipher_suites.len = SUPPORTED_CIPHER_SUITES_LEN;
        for (size_t i = 0; i < SUPPORTED_CIPHER_SUITES_LEN; i++) {
            error->supported_teep_cipher_suites.items[i] = supported_teep_cipher_suites[i];
        }
        error->contains |= TEEP_MESSAGE_CONTAINS_SUPPORTED_TEEP_CIPHER_SUITES;
    }
    return TEEP_SUCCESS;
}




/*!
    \brief      Create teep-query-response or teep-error message as a response to the teep-query-request message.

    \param[in]  update      Received teep-query-request message from the TAM.
    \param[in]  work_buf Scratch buffer allocated by caller to reduce malloc usage.
                        This buffer is used for temporary CBOR/EAT encoding work.
    \param[in]  app_name    Application filename to be requested.
    \param[in]  teep_agent_key_pair The Evidence includes the TEEP Agent’s public key.
    \param[out] message     Pointer of returned struct.

    \return     This returns only TEEP_SUCCESS;
 */
teep_err_t process_query_request(const teep_query_request_t *query_request,
                                          UsefulBuf work_buf,
                                          const char *app_name,
                                          teep_key_t *key_pair,
                                          teep_message_t *message)
{
    size_t i;
    uint64_t err_code_contains = 0;
    int32_t version = -1;
    teep_cipher_suite_t cipher_suite = TeepCipherSuiteInvalid;
    UsefulBufC eat = NULLUsefulBufC; /* CWT */
    teep_err_t          result;
    UsefulBuf tmp = work_buf;
    teep_query_response_t *query_response = (teep_query_response_t*)message;

    PRINT_DEBUG_LOG("[TEEP Agent] parsed TEEP QueryRequest message\n");
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_VERSIONS) {
        for (i = 0; i < query_request->versions.len; i++) {
            if (query_request->versions.items[i] == SUPPORTED_VERSION) {
                /* supported version is found */
                version = SUPPORTED_VERSION;
                break;
            }
        }
    }
    else {
        /* means version=0 is supported */
        version = 0;
    }

    if (version != SUPPORTED_VERSION) {
        err_code_contains |= TEEP_ERR_CODE_UNSUPPORTED_MSG_VERSION;
        goto error;
    }

    if (!(query_request->contains & TEEP_MESSAGE_CONTAINS_SUPPORTED_TEEP_CIPHER_SUITES)) {
        cipher_suite = supported_teep_cipher_suites[0];
    }
    for (i = 0; i < query_request->supported_teep_cipher_suites.len; i++) {
        for (size_t j = 0; j < SUPPORTED_CIPHER_SUITES_LEN; j++) {
            if (teep_cipher_suite_is_same(query_request->supported_teep_cipher_suites.items[i], supported_teep_cipher_suites[j])) {
                /* supported cipher suite is found */
                cipher_suite = supported_teep_cipher_suites[j];
                goto out;
            }
        }
    }

out:

    if (teep_cipher_suite_is_same(cipher_suite, TeepCipherSuiteInvalid)) {
        err_code_contains |= TEEP_ERR_CODE_UNSUPPORTED_CIPHER_SUITES;
        goto error;
    }

    PRINT_DEBUG_LOG("[TEEP Agent] generate QueryResponse\n");

    memset(query_response, 0, sizeof(teep_query_response_t));

    if (query_request->contains & TEEP_MESSAGE_CONTAINS_TOKEN) {
        query_response->token = query_request->token;
        query_response->contains |= TEEP_MESSAGE_CONTAINS_TOKEN;
    }

    (void)cipher_suite;
    query_response->selected_version = version;
    query_response->contains |= TEEP_MESSAGE_CONTAINS_SELECTED_VERSION;

    // Build QueryResponse and include attestation payload when requested.
    query_response->type = TEEP_TYPE_QUERY_RESPONSE;
    if(query_request->data_item_requested.attestation){
        PRINT_DEBUG_LOG("[TEEP Agent] generate attestation evidence\n");

        if(SGX_EVIDENCE == 1){
            PRINT_DEBUG_LOG("[TEEP Agent] evidence mode: SGX DCAP\n");
            result = create_evidence_dcap_envelope(query_request, tmp, key_pair, &eat);
        }else{
            PRINT_DEBUG_LOG("[TEEP Agent] evidence mode: generic EAT\n");
            result = create_evidence_generic(query_request, tmp, key_pair, &eat);
        }
        if (result != TEEP_SUCCESS) {
            PRINT_DEBUG_LOG("[TEEP Agent] evidence creation failed. %s(%d)\n",
                            teep_err_to_str(result),
                            result);
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }
        PRINT_DEBUG_LOG("[TEEP Agent] attestation evidence size=%zu capacity=%zu\n",
                        eat.len,
                        tmp.len);
        tmp = UsefulBuf_SliceTail(tmp, eat);
        if (tmp.ptr == NULL) {
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }
        query_response->contains |= TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD;

        query_response->attestation_payload =
            (teep_buf_t){.len = eat.len,
                         .ptr = const_cast<uint8_t *>(
                             static_cast<const uint8_t *>(eat.ptr))};
        if(SGX_EVIDENCE==1){
            query_response->attestation_payload_format =
                (teep_buf_t){ .len = sizeof(k_dcap_quote_attestation_payload_format) - 1,
                              .ptr = reinterpret_cast<const uint8_t *>(
                                  k_dcap_quote_attestation_payload_format) };
            query_response->contains |= TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD_FORMAT;
        }
    }

    if (query_request->data_item_requested.trusted_components) {
        if (app_name == NULL || app_name[0] == '\0') {
            err_code_contains |= TEEP_ERR_CODE_PERMANENT_ERROR;
            goto error;
        }

        QCBOREncodeContext requested_tc_context;
        UsefulBufC encoded_requested_component_id = NULLUsefulBufC;
        QCBOREncode_Init(&requested_tc_context, tmp);
        QCBOREncode_OpenArray(&requested_tc_context);
        QCBOREncode_AddBytes(&requested_tc_context,
                             (UsefulBufC){ .ptr = app_name, .len = strlen(app_name) });
        QCBOREncode_CloseArray(&requested_tc_context);
        if (QCBOREncode_Finish(&requested_tc_context, &encoded_requested_component_id) != QCBOR_SUCCESS) {
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }
        tmp = UsefulBuf_SliceTail(tmp, encoded_requested_component_id);
        query_response->requested_tc_list.len = 1;
        query_response->requested_tc_list.items[0].component_id =
            (teep_buf_t){ .len = encoded_requested_component_id.len,
                          .ptr = static_cast<const uint8_t *>(encoded_requested_component_id.ptr) };
        query_response->requested_tc_list.items[0].contains = 0;
        query_response->contains |= TEEP_MESSAGE_CONTAINS_REQUESTED_TC_LIST;

        size_t record_count = tc_manager_record_count();
        size_t tc_list_capacity = sizeof(query_response->tc_list.items) / sizeof(query_response->tc_list.items[0]);
        if (record_count > tc_list_capacity) {
            err_code_contains |= TEEP_ERR_CODE_PERMANENT_ERROR;
            goto error;
        }

        if (record_count > 0) {
            tc_list_item_t *tc_list_items_from_tc_manager =
                (tc_list_item_t *)malloc(record_count * sizeof(tc_list_item_t));
            if (tc_list_items_from_tc_manager == NULL) {
                err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
                goto error;
            }

            size_t tc_list_item_count = 0;
            if (tc_manager_get_tc_list(tc_list_items_from_tc_manager, record_count, &tc_list_item_count) != 0) {
                free(tc_list_items_from_tc_manager);
                err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
                goto error;
            }

            for (size_t tc_index = 0; tc_index < tc_list_item_count; tc_index++) {
                if (build_tc_info_cbor_from_tc_list_item(&tc_list_items_from_tc_manager[tc_index],
                                                         &query_response->tc_list.items[tc_index]) != 0) {
                    free(tc_list_items_from_tc_manager);
                    free_query_response_tc_list_buffers(query_response);
                    err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
                    goto error;
                }
            }

            query_response->tc_list.len = tc_list_item_count;
            query_response->contains |= TEEP_MESSAGE_CONTAINS_TC_LIST;
            free(tc_list_items_from_tc_manager);
        }
    }

    TEEP_DEBUG_QUERY_RESPONSE(query_response, 2, 2);

error: /* would be unneeded if the err-code becomes bit field */

    if (err_code_contains != 0) {
        return create_error(query_request->token, err_code_contains, NULLUsefulBuf, NULLUsefulBufC, message);
    }

    return TEEP_SUCCESS;

}


/*!
    \brief      Create teep-success or teep-error message as a response to the teep-update message.

    \param[in]  update      Received teep-update message from the TAM.
    \param[in]  work_buf Scratch buffer allocated by caller to reduce malloc usage.
    \param[out] message     Pointer of returned struct.

    \return     This returns only TEEP_SUCCESS;
 */
teep_err_t process_update(const teep_update_t *update,
                                   UsefulBuf work_buf,
                                   teep_message_t *message)
{
    suit_err_t result;
    teep_err_t teep_result = TEEP_ERR_UNEXPECTED_ERROR;
     UsefulBuf tmp = work_buf;

    #define NUM_PUBLIC_KEYS_FOR_ECDSA       1
    UsefulBufC public_keys_for_ecdsa[NUM_PUBLIC_KEYS_FOR_ECDSA] = {
        suit_manifest_esp256_cose_key_public,
    };

    suit_report_context_t *reporting_engine = NULL;
    suit_processor_context_t *processor_context = NULL;
    bool processor_initialized = false;
    suit_key_t sender_key;
    UsefulBufC nonce = NULLUsefulBufC;
    UsefulBuf envelope_buf = NULLUsefulBuf;
    bool report_invoke_pending = true;
    UsefulBufC envelope = NULLUsefulBufC;
    suit_process_flag_t process_flags = {0};
    size_t suit_report_len = 0;
    const uint8_t *suit_report = NULL;
    UsefulBufC error_suit_report = NULLUsefulBufC;
    teep_success_t *success = (teep_success_t *)message;
    uint64_t err_code_contains = 0;


     PRINT_DEBUG_LOG("[TEEP Agent] parsed TEEP Update message\n");
    if (!(update->contains & TEEP_MESSAGE_CONTAINS_TOKEN) ||
        update->token.len < 8 || 64 < update->token.len) {
        err_code_contains |= TEEP_ERR_CODE_PERMANENT_ERROR;
        goto error;
    }


    if (update->manifest_list.len < 1 ||
        update->manifest_list.len > TEEP_MAX_ARRAY_LENGTH) {
        PRINT_DEBUG_LOG("process_update : Invalid manifest list length. \n");
        err_code_contains |= TEEP_ERR_CODE_PERMANENT_ERROR;
        goto error;
    }

    for (size_t manifest_index = 0; manifest_index < update->manifest_list.len; manifest_index++) {
        reporting_engine = (suit_report_context_t*)calloc(1, sizeof(suit_report_context_t) + REPORT_SIZE);
        if (reporting_engine == NULL) {
            PRINT_DEBUG_LOG("main : Failed to allocate memory for suit_reporting_engine\n");
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }

        processor_context =
            (suit_processor_context_t*)calloc(1, sizeof(suit_processor_context_t) + SUIT_MAX_DATA_SIZE);
        if (processor_context == NULL) {
            PRINT_DEBUG_LOG("process_update : Failed to allocate memory for processor_context\n");
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }

        if (suit_set_suit_key_from_cose_key(suit_report_esp256_cose_key_private, &sender_key) != SUIT_SUCCESS) {
            PRINT_DEBUG_LOG("main : Failed to initialize sender key\n");
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }

        //Initialize SUIT Reporting Engine
        result = suit_report_init_engine(reporting_engine, REPORT_SIZE);
        if (result != SUIT_SUCCESS) {
            PRINT_DEBUG_LOG("main : Failed to initialize SUIT reporting engine. (%d)\n", result);
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }

        /* TODO: The sender key has not been sufficiently considered in the design*/
        /*
        result = suit_report_add_sender_key(reporting_engine, CBOR_TAG_COSE_SIGN1, T_COSE_ALGORITHM_RESERVED, &sender_key);
        if (result != SUIT_SUCCESS) {
            PRINT_DEBUG_LOG("main : Failed to set SUIT report sender key. (%d)\n", result);
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }
        */
        result = suit_report_start_encoding(reporting_engine, nonce);
        if (result != SUIT_SUCCESS) {
            PRINT_DEBUG_LOG("main : Failed to start SUIT report encoding. (%d)\n", result);
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }

        // Initialize SUIT Manifest Processor
        result = suit_processor_init(processor_context, SUIT_MAX_DATA_SIZE, reporting_engine, report_invoke_pending, &envelope_buf);
        if (result != SUIT_SUCCESS) {
            PRINT_DEBUG_LOG("process_update : Failed to initialize SUIT processor. (%d)\n", result);
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }
        processor_initialized = true;

        for (int i = 0; i < NUM_PUBLIC_KEYS_FOR_ECDSA; i++) {
            suit_key_t recipient_key = {};
            result = suit_set_suit_key_from_cose_key(public_keys_for_ecdsa[i], &recipient_key);
            if (result != SUIT_SUCCESS) {
                PRINT_DEBUG_LOG("\nprocess_update : Failed to initialize public key. (%d)\n", result);
                err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
                goto error;
            }
            result = suit_processor_add_recipient_key(
                processor_context,
                (int)CBOR_TAG_COSE_SIGN1,
                T_COSE_ALGORITHM_ESP256,
                &recipient_key);
            if (result != SUIT_SUCCESS) {
                PRINT_DEBUG_LOG("\nprocess_update : Failed to add recipient key. (%d)\n", result);
                err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
                goto error;
            }
        }

        // Read envelope (contains SUIT manifest).
        PRINT_DEBUG_LOG("[TEEP Agent] process SUIT Manifest\n");
        if (update->manifest_list.items[manifest_index].ptr == NULL ||
            update->manifest_list.items[manifest_index].len == 0 ||
            update->manifest_list.items[manifest_index].len > envelope_buf.len) {
            PRINT_DEBUG_LOG("process_update : Failed to read Manifest. \n");
            err_code_contains |= TEEP_ERR_CODE_PERMANENT_ERROR;
            goto error;
        }

        memcpy(envelope_buf.ptr,
               update->manifest_list.items[manifest_index].ptr,
               update->manifest_list.items[manifest_index].len);
        envelope_buf.len = update->manifest_list.items[manifest_index].len;
        envelope = UsefulBuf_Const(envelope_buf);

        // Process envelope.
        process_flags.all = UINT16_MAX;
        process_flags.uninstall = 0;
        result = suit_processor_add_manifest(processor_context, envelope, process_flags);
        if (result != SUIT_SUCCESS) {
            PRINT_DEBUG_LOG("process_update : Failed to add manifest. (%d)\n", result);
            err_code_contains |= TEEP_ERR_CODE_PERMANENT_ERROR;
            goto error;
        }
        result = suit_process_envelope(processor_context);
        if (result != SUIT_SUCCESS) {
            size_t fail_report_len = 0;
            const uint8_t *fail_report = suit_get_suit_report(&fail_report_len);
            const char *err_str = suit_err_to_str(result);
            PRINT_DEBUG_LOG("process_update : Failed to install and invoke a Manifest. (%d)%s%s\n",
                   result,
                   err_str != NULL ? " " : "",
                   err_str != NULL ? err_str : "");
            if (fail_report != NULL && fail_report_len > 0) {
                PRINT_DEBUG_LOG("[TEEP Agent] SUIT report available on failure (%zu bytes)\n",
                       fail_report_len);
                suit_report_args_t report_args = {
                    .suit_report = (UsefulBufC){ .ptr = fail_report, .len = fail_report_len },
                };
                suit_print_report(report_args);
                if (fail_report_len <= tmp.len) {
                    memcpy(tmp.ptr, fail_report, fail_report_len);
                    error_suit_report = (UsefulBufC){ .ptr = tmp.ptr, .len = fail_report_len };
                } else {
                    PRINT_DEBUG_LOG("process_update : SUIT report is too large for scratch buffer (%zu > %zu)\n",
                           fail_report_len, tmp.len);
                }
            }
            err_code_contains |= TEEP_ERR_CODE_MANIFEST_PROCESSING_FAILED;
            goto error;
        }

        if (tc_manager_check_and_update_record(processor_context->b.manifest_digest) != 0) {
            PRINT_DEBUG_LOG("process_update : Failed to check and update manifest record.\n");
            result = SUIT_ERR_FATAL;
            err_code_contains |= TEEP_ERR_CODE_TEMPORARY_ERROR;
            goto error;
        }

        if (processor_initialized) {
            suit_processor_free(processor_context);
            processor_initialized = false;
        }
        free(processor_context);
        processor_context = NULL;
        free(reporting_engine);
        reporting_engine = NULL;
    }

    // create SUCCESS message
    success->type = TEEP_TYPE_SUCCESS;
    success->contains = TEEP_MESSAGE_CONTAINS_TOKEN;
    success->token = update->token;
    suit_report = suit_get_suit_report(&suit_report_len);
    if (suit_report != NULL && suit_report_len > 0) {
        success->suit_reports.len = 1;
        success->suit_reports.items[0] = (teep_buf_t){
            .len = suit_report_len,
            .ptr = const_cast<uint8_t *>(
                static_cast<const uint8_t *>(suit_report)),
        };
        success->contains |= TEEP_MESSAGE_CONTAINS_SUIT_REPORTS;
        PRINT_DEBUG_LOG("[TEEP Broker] > SUIT report in success (%zu bytes)\n", suit_report_len);
    }
    tc_manager_dump_records();

    teep_result = TEEP_SUCCESS;

error:

    if (processor_initialized) {
        suit_processor_free(processor_context);
    }
    if(processor_context != NULL) {
        free(processor_context);
    }
    if(reporting_engine != NULL) {
        free(reporting_engine);
    }
    if (err_code_contains != 0) {
        return create_error(update->token, err_code_contains, NULLUsefulBuf, error_suit_report, message);
    }
    return teep_result;
}

// global status
typedef enum teep_agent_status {
        WAITING_QUERY_REQUEST,
        WAITING_UPDATE_OR_QUERY_REQUEST,
    } teep_agent_status_t;
teep_agent_status_t g_agent_status = WAITING_QUERY_REQUEST;

extern "C" ecall_process_teep_result_t ecall_process_message(const uint8_t *recv_buf,
                                                             size_t recv_len,
                                                             const char *app_name,
                                                             uint8_t *send_buf,
                                                             size_t allocated_len,
                                                             size_t *actual_len)
{
    teep_err_t result;

    teep_message_t recv_message;
    teep_message_t send_message;

    UsefulBuf_MAKE_STACK_UB(cbor_send_buf, MAX_SEND_BUFFER_SIZE);

    UsefulBuf_MAKE_STACK_UB(work_buf, WORK_BUF_LEN);
    work_buf.len = WORK_BUF_LEN; /* Caller-side stack scratch buffer to reduce malloc in subroutines. */
    PRINT_DEBUG_LOG("[TEEP Broker] buffers: cbor_send=%zu work=%zu allocated_send=%zu recv=%zu\n",
                    (size_t)MAX_SEND_BUFFER_SIZE,
                    (size_t)WORK_BUF_LEN,
                    allocated_len,
                    recv_len);

    if (g_key_state != TEEP_KEY_READY) {
        PRINT_DEBUG_LOG("main : key not initialized.\n");
        return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }

    // Verify and print QueryRequest cose.
    UsefulBufC payload;
    tam_verify.cose_tag = CBOR_TAG_COSE_SIGN1;
    UsefulBufC recv_buf_const = (UsefulBufC){ .ptr = recv_buf, .len = recv_len };
    result = teep_verify_cose_sign1(recv_buf_const, &tam_verify, &payload);
    if (result != TEEP_SUCCESS) {
        tam_verify.cose_tag = CBOR_TAG_COSE_SIGN;
        result = teep_verify_cose_sign(recv_buf_const, &tam_verify, 1, &payload);
    }
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to verify TEEP message. %s(%d)\n", teep_err_to_str(result), result);
        return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }

    result = teep_set_message_from_bytes(
        const_cast<uint8_t *>(static_cast<const uint8_t *>(payload.ptr)),
        payload.len, &recv_message);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to decode TEEP message payload. %s(%d)\n", teep_err_to_str(result), result);
        return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }


    switch (recv_message.teep_message.type) {
        case TEEP_TYPE_QUERY_REQUEST:
            PRINT_DEBUG_LOG("[TEEP Broker] < Received QueryRequest.\n");
            TEEP_DEBUG_QUERY((const teep_query_request_t *)&recv_message, 2, 2);
            result = process_query_request((const teep_query_request_t *)&recv_message,
                                           work_buf,
                                           app_name,
                                           &agent_sign.key,
                                           &send_message);
            break;
        case TEEP_TYPE_UPDATE:
            PRINT_DEBUG_LOG("[TEEP Broker] < Received UpdateMessage.\n");
            TEEP_DEBUG_UPDATE((const teep_update_t *)&recv_message, 2, 2, NULL);
            if (g_agent_status == WAITING_QUERY_REQUEST) {
                PRINT_DEBUG_LOG("main : Received Update message without QueryRequest.\n");
                break;
            }
            result = process_update((const teep_update_t *)&recv_message, work_buf, &send_message);

            break;
        default:
            PRINT_DEBUG_LOG("main : Unexpected message type %d\n.", recv_message.teep_message.type);
            return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }

    if (result != TEEP_SUCCESS) {
            PRINT_DEBUG_LOG("main : Failed to create teep message. %s(%d)\n", teep_err_to_str(result), result);
            return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }
    if (g_agent_status == WAITING_QUERY_REQUEST &&
        send_message.teep_message.type == TEEP_TYPE_QUERY_RESPONSE) {
        g_agent_status = WAITING_UPDATE_OR_QUERY_REQUEST;
    }else if (g_agent_status == WAITING_UPDATE_OR_QUERY_REQUEST &&
        send_message.teep_message.type == TEEP_TYPE_SUCCESS) {
        g_agent_status = WAITING_QUERY_REQUEST;
    }


    // Convert send_message to CBOR and sign it
    cbor_send_buf.len = MAX_SEND_BUFFER_SIZE;
    result = teep_encode_message(&send_message, &cbor_send_buf.ptr, &cbor_send_buf.len);
    if (result != TEEP_SUCCESS) {
        if (send_message.teep_message.type == TEEP_TYPE_QUERY_RESPONSE) {
            free_query_response_tc_list_buffers(&send_message.query_response);
        }
        PRINT_DEBUG_LOG("main : Failed to encode query_response message. %s(%d) capacity=%zu\n",
                        teep_err_to_str(result),
                        result,
                        (size_t)MAX_SEND_BUFFER_SIZE);
        return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }
    PRINT_DEBUG_LOG("[TEEP Broker] encoded TEEP message size=%zu capacity=%zu\n",
                    cbor_send_buf.len,
                    (size_t)MAX_SEND_BUFFER_SIZE);

    UsefulBuf cose_send_buf = (UsefulBuf){ .ptr = send_buf, .len = allocated_len };
    cose_send_buf.len = allocated_len;
    agent_sign.cose_tag = CBOR_TAG_COSE_SIGN1;
    result = teep_sign_cose_sign1(UsefulBuf_Const(cbor_send_buf), &agent_sign, &cose_send_buf);
    if (result != TEEP_SUCCESS) {
        if (send_message.teep_message.type == TEEP_TYPE_QUERY_RESPONSE) {
            free_query_response_tc_list_buffers(&send_message.query_response);
        }
        PRINT_DEBUG_LOG("main : Failed to sign to query_response message. %s(%d) cbor_len=%zu allocated_len=%zu\n",
                        teep_err_to_str(result),
                        result,
                        cbor_send_buf.len,
                        allocated_len);
        return ECALL_PROCESS_TEEP_RESULT_FATAL;
    }
    PRINT_DEBUG_LOG("[TEEP Broker] signed COSE message size=%zu allocated_len=%zu\n",
                    cose_send_buf.len,
                    allocated_len);

    if (actual_len != NULL) {
        *actual_len = cose_send_buf.len;
    }

    if (send_message.teep_message.type == TEEP_TYPE_QUERY_RESPONSE) {
        free_query_response_tc_list_buffers(&send_message.query_response);
    }

    if (send_message.teep_message.type == TEEP_TYPE_ERROR) {
        return ECALL_PROCESS_TEEP_RESULT_RESPONSE_IS_TEEP_ERROR;
    }
    if (send_message.teep_message.type == TEEP_TYPE_QUERY_RESPONSE &&
        (send_message.query_response.contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD) != 0) {
        return ECALL_PROCESS_TEEP_RESULT_DEVICE_ACTIVATION_FLOW;
    }

    return ECALL_PROCESS_TEEP_RESULT_OK;
}
