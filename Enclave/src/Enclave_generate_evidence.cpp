/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <teep/teep_message_print.h>
#include "teep/teep_cose.h"
#include "teep/teep_common.h"
#include "teep/teep_message_data.h"
#ifdef __cplusplus
}
#endif

#include "teep_create_evidence.h"
#include "Enclave.h"
#include "debug_print.h"
#include "attester_es256_key.h"

#include "sgx_error.h"
#include "sgx_report.h"
#include "sgx_tcrypto.h"
#include "sgx_utils.h"

extern "C" sgx_status_t ocall_get_qe_target_info(sgx_status_t *retval,
                                                  sgx_target_info_t *qe_target_info);
extern "C" sgx_status_t ocall_get_quote_size(sgx_status_t *retval,
                                              uint32_t *quote_size);
extern "C" sgx_status_t ocall_get_quote(sgx_status_t *retval,
                                         const sgx_report_t *report,
                                         uint8_t *quote_buf,
                                         uint32_t quote_size);


enum{
    /* common */
    EAT_PROFILE = 265,
    MEASUREMENT_VALUE = 2,

    /* generic-eat */
    CNF = 8,
    COSE_KEY = 1,
    KEY_TYPE = 1,
    CURVE = -1,
    X_COORDINATE = -2,
    Y_COORDINATE = -3,
    KEY_ID = 3,
    EAT_NONCE = 10,
    UEID = 256,
    OEMID = 258,
    HWMODEL = 259,
    HWVERSION = 260,
    MEASUREMENTS =273,
    CONTENT_TYPE = 600,
    ID = 1,
    ALGORITHM = 1,

    /* TAWS SGX DCAP evidence envelope */
    TEEP_CHALLENGE = 2,
    TAWS_DCAP_QUOTE = -70000
};

static teep_err_t teep_err_from_sgx_status(sgx_status_t status)
{
    return (status == SGX_SUCCESS) ? TEEP_SUCCESS : TEEP_ERR_UNEXPECTED_ERROR;
}

static teep_err_t create_dcap_report_data(const teep_query_request_t *query_request,
                                          const teep_key_t *key_pair,
                                          sgx_report_data_t *report_data)
{
    sgx_status_t sgx_ret = SGX_SUCCESS;
    sgx_sha_state_handle_t sha_handle = NULL;
    sgx_sha384_hash_t digest = {0};

    if (query_request == NULL || key_pair == NULL || key_pair->public_key == NULL ||
        key_pair->public_key_len != PRIME256V1_PUBLIC_KEY_LENGTH ||
        key_pair->public_key[0] != 0x04 ||
        report_data == NULL) {
        return TEEP_ERR_INVALID_VALUE;
    }

    memset(report_data, 0, sizeof(*report_data));

    sgx_ret = sgx_sha384_init(&sha_handle);
    if (sgx_ret != SGX_SUCCESS) {
        return teep_err_from_sgx_status(sgx_ret);
    }

    sgx_ret = sgx_sha384_update(key_pair->public_key + 1,
                                32,
                                sha_handle);
    if (sgx_ret == SGX_SUCCESS) {
        sgx_ret = sgx_sha384_update(key_pair->public_key + 33,
                                    32,
                                    sha_handle);
    }
    if (sgx_ret == SGX_SUCCESS &&
        (query_request->contains & TEEP_MESSAGE_CONTAINS_CHALLENGE) &&
        query_request->challenge.ptr != NULL &&
        query_request->challenge.len > 0) {
        sgx_ret = sgx_sha384_update((const uint8_t *)query_request->challenge.ptr,
                                    (uint32_t)query_request->challenge.len,
                                    sha_handle);
    }
    if (sgx_ret == SGX_SUCCESS) {
        sgx_ret = sgx_sha384_get_hash(sha_handle, &digest);
    }

    sgx_status_t close_ret = sgx_sha384_close(sha_handle);
    if (sgx_ret != SGX_SUCCESS) {
        return teep_err_from_sgx_status(sgx_ret);
    }
    if (close_ret != SGX_SUCCESS) {
        return teep_err_from_sgx_status(close_ret);
    }

    memcpy(report_data->d, digest, sizeof(digest));
    return TEEP_SUCCESS;
}

teep_err_t create_evidence_generic(const teep_query_request_t *query_request,
                                 UsefulBuf buf,
                                 teep_key_t *key_pair,
                                 UsefulBufC *ret)
{
    (void)query_request;

    struct t_cose_sign1_sign_ctx sign_ctx;
    enum t_cose_err_t cose_result;

    QCBOREncodeContext context;

    /* Initialize for signing */
    teep_err_t          result;
    teep_mechanism_t mechanism_sign;
    result = teep_key_init_es256_key_pair(attester_es256_private_key, attester_es256_public_key, NULLUsefulBufC, &mechanism_sign.key);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to create t_cose key pair. %s(%d)\n", teep_err_to_str(result), result);
        return result;
    }

    t_cose_sign1_sign_init(&sign_ctx, 0, T_COSE_ALGORITHM_ES256);
    t_cose_sign1_set_signing_key(&sign_ctx, mechanism_sign.key.cose_key, mechanism_sign.key.kid);
    
    /* encode the header */
    QCBOREncode_Init(&context, buf);
    enum t_cose_err_t t_cose_result = t_cose_sign1_encode_parameters(&sign_ctx, &context);
    if (t_cose_result != T_COSE_SUCCESS) {
        return TEEP_ERR_SIGNING_FAILED;
    }


    /* encoding payload start */
    QCBOREncode_OpenMap(&context);

    /* confirmation */
    //cnf/8: {/ COSE_Key /1:{/kty/1:2, /crv/-1:1, /x/-2:h'...', /y/-3:h'...'},/kid/3:h'...'}
    int64_t kty = 2; // EC2
    int64_t crv = 1; // P-256
    unsigned char public_key_x[32];
    unsigned char public_key_y[32];
    memcpy(public_key_x, key_pair->public_key+1, 32);
    memcpy(public_key_y, key_pair->public_key+33, 32);


    QCBOREncode_OpenMapInMapN(&context, CNF); // open cnf map
    QCBOREncode_OpenMapInMapN(&context, COSE_KEY); // open cose_key map
    QCBOREncode_AddInt64ToMapN(&context, KEY_TYPE, kty); // key type
    QCBOREncode_AddInt64ToMapN(&context, CURVE, crv); //curve
    QCBOREncode_AddBytesToMapN(&context, X_COORDINATE, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(public_key_x)); // x
    QCBOREncode_AddBytesToMapN(&context, Y_COORDINATE, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(public_key_y)); // y
    QCBOREncode_CloseMap(&context); // close cose_key map
    QCBOREncode_AddBytesToMapN(&context, KEY_ID, key_pair->kid); // kid
    QCBOREncode_CloseMap(&context); // close cnf map
    
    /* eat_nonce */
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_CHALLENGE) {
        QCBOREncode_AddBytesToMapN(&context, EAT_NONCE, (UsefulBufC){.ptr = query_request->challenge.ptr, .len = query_request->challenge.len});
    } else {
        const uint8_t eat_nonce[] = {0x94, 0x8F, 0x88, 0x60, 0xD1, 0x3A, 0x46, 0x3E, 0x8E};
        QCBOREncode_AddBytesToMapN(&context, EAT_NONCE, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(eat_nonce));
    }
    /* ueid */
    uint8_t ueid[] = {0x01, 0x62, 0x75, 0x69, 0x6c, 0x64, 0x69, 0x6e, 0x67, 0x2d, 0x64, 0x65, 0x76, 0x2d, 0x31, 0x32, 0x33};
    QCBOREncode_AddBytesToMapN(&context, UEID, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(ueid));

    /* oemid */
    uint8_t oemid[] = {0x89, 0x48, 0x23};
    QCBOREncode_AddBytesToMapN(&context, OEMID, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(oemid));

    /* hwmodel */
    uint8_t hwmodel[] = {0x54, 0x9d, 0xce, 0xcc, 0x8b, 0x98, 0x7c, 0x73, 0x7b, 0x44, 0xe4, 0x0f, 0x7c, 0x63, 0x5c, 0xe8};
    QCBOREncode_AddBytesToMapN(&context, HWMODEL, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(hwmodel));

    /* hwversion */
    QCBOREncode_OpenArrayInMapN(&context, HWVERSION);
    QCBOREncode_AddText(&context, UsefulBuf_FROM_SZ_LITERAL("1.3.4"));
    QCBOREncode_AddInt64(&context, 1);
    QCBOREncode_CloseArray(&context);

    /* eat_profile */
    QCBOREncode_AddTextToMapN(&context, EAT_PROFILE, UsefulBuf_FROM_SZ_LITERAL("urn:ietf:rfc:rfc9711"));

    /* measurements */
    static const uint8_t deadbeef_bytes[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF
    };

    QCBOREncode_OpenArrayInMapN(&context, MEASUREMENTS); // open measurements array
    QCBOREncode_OpenArray(&context);  // open measurements inner array

    QCBOREncode_AddInt64(&context, CONTENT_TYPE); //content-type
    QCBOREncode_BstrWrap(&context); // open bstr wrap
    QCBOREncode_OpenMap(&context); // open content-format map
    QCBOREncode_OpenArrayInMapN(&context, ID); // open id array
    QCBOREncode_AddText(&context, UsefulBuf_FROM_SZ_LITERAL("TEEP Agent")); //name
    QCBOREncode_OpenArray(&context); // open version array
    QCBOREncode_AddText(&context, UsefulBuf_FROM_SZ_LITERAL("1.3.4"));
    QCBOREncode_AddInt64(&context, 1);
    QCBOREncode_CloseArray(&context); // close version array
    QCBOREncode_CloseArray(&context); // close id array
    QCBOREncode_OpenArrayInMapN(&context, MEASUREMENT_VALUE); // open measurement array
    QCBOREncode_AddInt64(&context, ALGORITHM); //alg
    QCBOREncode_AddBytes(&context, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(deadbeef_bytes)); //value
    QCBOREncode_CloseArray(&context); // close measurement array
    QCBOREncode_CloseMap(&context); // close content-format map
    QCBOREncode_CloseBstrWrap(&context, NULL); // close bstr wrap

    QCBOREncode_CloseArray(&context); // close measurements inner array
    QCBOREncode_CloseArray(&context); // close measurements array


    /* encoding payload end */
    QCBOREncode_CloseMap(&context);

    
    /* sign */
    cose_result = t_cose_sign1_encode_signature(&sign_ctx, &context);
    if (cose_result != T_COSE_SUCCESS) {
        return TEEP_ERR_SIGNING_FAILED;
    }
    
    /* complete CBOR Encoding */
    QCBORError error = QCBOREncode_Finish(&context, ret);
    if (error != QCBOR_SUCCESS) {
        PRINT_DEBUG_LOG("QCBOREncode_Finish() = %d\n", error);
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    teep_free_key(&mechanism_sign.key);
    return TEEP_SUCCESS;
}

teep_err_t create_evidence_dcap(const teep_query_request_t *query_request,
                                 UsefulBuf buf,
                                 teep_key_t *key_pair,
                                 UsefulBufC *ret)
{
    sgx_status_t sgx_ret = SGX_SUCCESS;
    teep_err_t result = TEEP_SUCCESS;

    if (query_request == NULL || key_pair == NULL || ret == NULL || buf.ptr == NULL || buf.len == 0) {
        return TEEP_ERR_INVALID_VALUE;
    }
    *ret = NULLUsefulBufC;

    /* Get QE target info for an application report addressed to the quoting enclave. */
    sgx_target_info_t qe_target_info = { 0 };
    sgx_status_t ocall_ret = SGX_SUCCESS;
    PRINT_DEBUG_LOG("[TEEP Agent] DCAP: get QE target info\n");
    sgx_ret = ocall_get_qe_target_info(&ocall_ret, &qe_target_info);
    if (sgx_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: ocall_get_qe_target_info bridge failed 0x%04x\n", sgx_ret);
        return teep_err_from_sgx_status(sgx_ret);
    }
    if (ocall_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: sgx_qe_get_target_info failed 0x%04x\n", ocall_ret);
        return teep_err_from_sgx_status(ocall_ret);
    }

    /* Bind the quote to the TEEP Agent public key and the TAM challenge. */
    sgx_report_data_t report_data = {};
    result = create_dcap_report_data(query_request, key_pair, &report_data);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    sgx_report_t report = {};
    PRINT_DEBUG_LOG("[TEEP Agent] DCAP: create report\n");
    sgx_ret = sgx_create_report(&qe_target_info, &report_data, &report);
    if (sgx_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: sgx_create_report failed 0x%04x\n", sgx_ret);
        return teep_err_from_sgx_status(sgx_ret);
    }

    uint32_t required_quote_size = 0;
    ocall_ret = SGX_SUCCESS;
    PRINT_DEBUG_LOG("[TEEP Agent] DCAP: get quote size\n");
    sgx_ret = ocall_get_quote_size(&ocall_ret, &required_quote_size);
    if (sgx_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: ocall_get_quote_size bridge failed 0x%04x\n", sgx_ret);
        return teep_err_from_sgx_status(sgx_ret);
    }
    if (ocall_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: sgx_qe_get_quote_size failed 0x%04x\n", ocall_ret);
        return teep_err_from_sgx_status(ocall_ret);
    }
    if (required_quote_size == 0 || required_quote_size > buf.len) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: quote buffer too small (required=%u capacity=%zu)\n",
                        required_quote_size,
                        buf.len);
        return TEEP_ERR_NO_MEMORY;
    }

    ocall_ret = SGX_SUCCESS;
    PRINT_DEBUG_LOG("[TEEP Agent] DCAP: get quote (%u bytes)\n", required_quote_size);
    sgx_ret = ocall_get_quote(&ocall_ret, &report, (uint8_t *)buf.ptr, required_quote_size);
    if (sgx_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: ocall_get_quote bridge failed 0x%04x\n", sgx_ret);
        return teep_err_from_sgx_status(sgx_ret);
    }
    if (ocall_ret != SGX_SUCCESS) {
        PRINT_DEBUG_LOG("[TEEP Agent] DCAP: sgx_qe_get_quote failed 0x%04x\n", ocall_ret);
        return teep_err_from_sgx_status(ocall_ret);
    }

    ret->ptr = buf.ptr;
    ret->len = required_quote_size;
    return TEEP_SUCCESS;
}

teep_err_t create_evidence_dcap_envelope(const teep_query_request_t *query_request,
                                         UsefulBuf buf,
                                         teep_key_t *key_pair,
                                         UsefulBufC *ret)
{
    if (query_request == NULL || key_pair == NULL || ret == NULL ||
        buf.ptr == NULL || buf.len == 0 ||
        key_pair->public_key == NULL ||
        key_pair->public_key_len != PRIME256V1_PUBLIC_KEY_LENGTH ||
        key_pair->public_key[0] != 0x04) {
        return TEEP_ERR_INVALID_VALUE;
    }
    *ret = NULLUsefulBufC;

    size_t challenge_len = 0;
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_CHALLENGE) {
        if (query_request->challenge.len > 0 && query_request->challenge.ptr == NULL) {
            return TEEP_ERR_INVALID_VALUE;
        }
        challenge_len = query_request->challenge.len;
    }

    uint8_t *quote_buf = (uint8_t *)malloc(buf.len);
    if (quote_buf == NULL) {
        return TEEP_ERR_NO_MEMORY;
    }

    UsefulBuf quote_storage = { .ptr = quote_buf, .len = buf.len };
    UsefulBufC quote = NULLUsefulBufC;
    teep_err_t result = create_evidence_dcap(query_request,
                                             quote_storage,
                                             key_pair,
                                             &quote);
    if (result != TEEP_SUCCESS) {
        free(quote_buf);
        return result;
    }

    size_t raw_report_data_len = 64 + challenge_len;
    uint8_t *raw_report_data_buf = (uint8_t *)malloc(raw_report_data_len);
    if (raw_report_data_buf == NULL) {
        free(quote_buf);
        return TEEP_ERR_NO_MEMORY;
    }
    memcpy(raw_report_data_buf, key_pair->public_key + 1, 32);
    memcpy(raw_report_data_buf + 32, key_pair->public_key + 33, 32);
    if (challenge_len > 0) {
        memcpy(raw_report_data_buf + 64, query_request->challenge.ptr, challenge_len);
    }
    UsefulBufC raw_report_data = { .ptr = raw_report_data_buf, .len = raw_report_data_len };

    //attestation-payload: << [ raw-dcap-quote3, raw-report-data: << x || y || nonce >> ] >>
    size_t encoded_report_data_len = 0;
    QCBOREncodeContext report_data_size_context;
    QCBOREncode_Init(&report_data_size_context, SizeCalculateUsefulBuf);
    QCBOREncode_AddBytes(&report_data_size_context, raw_report_data);
    if (QCBOREncode_FinishGetSize(&report_data_size_context, &encoded_report_data_len) != QCBOR_SUCCESS ||
        encoded_report_data_len == 0) {
        free(raw_report_data_buf);
        free(quote_buf);
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    uint8_t *encoded_report_data_buf = (uint8_t *)malloc(encoded_report_data_len);
    if (encoded_report_data_buf == NULL) {
        free(raw_report_data_buf);
        free(quote_buf);
        return TEEP_ERR_NO_MEMORY;
    }

    QCBOREncodeContext report_data_context;
    UsefulBufC encoded_report_data = NULLUsefulBufC;
    QCBOREncode_Init(&report_data_context,
                     (UsefulBuf){ .ptr = encoded_report_data_buf,
                                  .len = encoded_report_data_len });
    QCBOREncode_AddBytes(&report_data_context, raw_report_data);
    QCBORError error = QCBOREncode_Finish(&report_data_context, &encoded_report_data);
    if (error != QCBOR_SUCCESS) {
        PRINT_DEBUG_LOG("QCBOREncode_Finish() = %d\n", error);
        free(encoded_report_data_buf);
        free(raw_report_data_buf);
        free(quote_buf);
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    QCBOREncodeContext context;
    QCBOREncode_Init(&context, buf);
    QCBOREncode_OpenArray(&context);
    QCBOREncode_AddBytes(&context, quote);
    QCBOREncode_AddBytes(&context, encoded_report_data);
    QCBOREncode_CloseArray(&context);

    error = QCBOREncode_Finish(&context, ret);
    free(encoded_report_data_buf);
    free(raw_report_data_buf);
    free(quote_buf);
    if (error != QCBOR_SUCCESS) {
        PRINT_DEBUG_LOG("QCBOREncode_Finish() = %d\n", error);
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    return TEEP_SUCCESS;
}
