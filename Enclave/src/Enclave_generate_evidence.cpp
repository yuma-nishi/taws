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
#include "teep/teep_cose.h"
#include "teep/teep_common.h"
#include "teep/teep_message_data.h"
#ifdef __cplusplus
}
#endif

#include "teep_create_evidence.h"
#include "debug_print.h"
#include "attester_esp256_key.h"

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
extern "C" int printf(const char *fmt, ...);


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
    ALGORITHM = 1
};

static const size_t kP256CoordinateLength = 32;
static const uint8_t kDefaultEatNonce[] = {0x94, 0x8F, 0x88, 0x60, 0xD1, 0x3A, 0x46, 0x3E, 0x8E};
static const uint8_t kUeid[] = {0x01, 0x62, 0x75, 0x69, 0x6c, 0x64, 0x69, 0x6e, 0x67, 0x2d, 0x64, 0x65, 0x76, 0x2d, 0x31, 0x32, 0x33};
static const uint8_t kOemid[] = {0x89, 0x48, 0x23};
static const uint8_t kHwmodel[] = {0x54, 0x9d, 0xce, 0xcc, 0x8b, 0x98, 0x7c, 0x73, 0x7b, 0x44, 0xe4, 0x0f, 0x7c, 0x63, 0x5c, 0xe8};
static const uint8_t kDeadbeefBytes[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF,
    0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF
};

static teep_err_t teep_err_from_sgx_status(sgx_status_t status)
{
    return (status == SGX_SUCCESS) ? TEEP_SUCCESS : TEEP_ERR_UNEXPECTED_ERROR;
}

static bool is_uncompressed_p256_public_key(const teep_key_t *key_pair)
{
    return key_pair != NULL &&
           key_pair->public_key != NULL &&
           key_pair->public_key_len == PRIME256V1_PUBLIC_KEY_LENGTH &&
           key_pair->public_key[0] == 0x04;
}

static teep_err_t get_query_challenge(const teep_query_request_t *query_request,
                                      UsefulBufC *challenge)
{
    if (query_request == NULL || challenge == NULL) {
        return TEEP_ERR_INVALID_VALUE;
    }

    *challenge = NULLUsefulBufC;
    if (!(query_request->contains & TEEP_MESSAGE_CONTAINS_CHALLENGE)) {
        return TEEP_SUCCESS;
    }

    if (query_request->challenge.len > 0 && query_request->challenge.ptr == NULL) {
        return TEEP_ERR_INVALID_VALUE;
    }

    *challenge = (UsefulBufC){
        .ptr = query_request->challenge.ptr,
        .len = query_request->challenge.len
    };
    return TEEP_SUCCESS;
}

static teep_err_t create_raw_dcap_report_data(const teep_query_request_t *query_request,
                                              const teep_key_t *key_pair,
                                              uint8_t **raw_report_data_buf,
                                              UsefulBufC *raw_report_data)
{
    if (!is_uncompressed_p256_public_key(key_pair) ||
        raw_report_data_buf == NULL ||
        raw_report_data == NULL) {
        return TEEP_ERR_INVALID_VALUE;
    }

    UsefulBufC challenge = NULLUsefulBufC;
    teep_err_t result = get_query_challenge(query_request, &challenge);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    size_t raw_report_data_len = (2 * kP256CoordinateLength) + challenge.len;
    uint8_t *buf = (uint8_t *)malloc(raw_report_data_len);
    if (buf == NULL) {
        return TEEP_ERR_NO_MEMORY;
    }

    memcpy(buf, key_pair->public_key + 1, kP256CoordinateLength);
    memcpy(buf + kP256CoordinateLength, key_pair->public_key + 1 + kP256CoordinateLength, kP256CoordinateLength);
    if (challenge.len > 0) {
        memcpy(buf + (2 * kP256CoordinateLength), challenge.ptr, challenge.len);
    }

    *raw_report_data_buf = buf;
    *raw_report_data = (UsefulBufC){ .ptr = buf, .len = raw_report_data_len };
    return TEEP_SUCCESS;
}

static teep_err_t create_dcap_report_data(const teep_query_request_t *query_request,
                                          const teep_key_t *key_pair,
                                          sgx_report_data_t *report_data)
{
    sgx_status_t sgx_ret = SGX_SUCCESS;
    sgx_sha_state_handle_t sha_handle = NULL;
    sgx_sha384_hash_t digest = {0};

    if (!is_uncompressed_p256_public_key(key_pair) || report_data == NULL) {
        return TEEP_ERR_INVALID_VALUE;
    }

    UsefulBufC challenge = NULLUsefulBufC;
    teep_err_t result = get_query_challenge(query_request, &challenge);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    memset(report_data, 0, sizeof(*report_data));

    sgx_ret = sgx_sha384_init(&sha_handle);
    if (sgx_ret != SGX_SUCCESS) {
        return teep_err_from_sgx_status(sgx_ret);
    }

    sgx_ret = sgx_sha384_update(key_pair->public_key + 1,
                                kP256CoordinateLength,
                                sha_handle);
    if (sgx_ret == SGX_SUCCESS) {
        sgx_ret = sgx_sha384_update(key_pair->public_key + 1 + kP256CoordinateLength,
                                    kP256CoordinateLength,
                                    sha_handle);
    }
    if (sgx_ret == SGX_SUCCESS && challenge.len > 0) {
        sgx_ret = sgx_sha384_update((const uint8_t *)challenge.ptr,
                                    (uint32_t)challenge.len,
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
    if (!is_uncompressed_p256_public_key(key_pair) ||
        query_request == NULL ||
        ret == NULL ||
        buf.ptr == NULL ||
        buf.len == 0) {
        return TEEP_ERR_INVALID_VALUE;
    }
    *ret = NULLUsefulBufC;

    UsefulBufC challenge = NULLUsefulBufC;
    teep_err_t result = get_query_challenge(query_request, &challenge);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    struct t_cose_sign1_sign_ctx sign_ctx;
    enum t_cose_err_t cose_result;
    enum t_cose_err_t t_cose_result = T_COSE_SUCCESS;
    QCBORError error = QCBOR_SUCCESS;

    QCBOREncodeContext context;

    /* Initialize for signing */
    teep_mechanism_t mechanism_sign = {};
    bool key_initialized = false;
    UsefulBufC x_coordinate = { .ptr = key_pair->public_key + 1, .len = kP256CoordinateLength };
    UsefulBufC y_coordinate = { .ptr = key_pair->public_key + 1 + kP256CoordinateLength, .len = kP256CoordinateLength };

    result = teep_key_init_esp256_key_pair(attester_esp256_private_key, attester_esp256_public_key, NULLUsefulBufC, &mechanism_sign.key);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to create t_cose key pair. %s(%d)\n", teep_err_to_str(result), result);
        goto cleanup;
    }
    key_initialized = true;

    t_cose_sign1_sign_init(&sign_ctx, 0, T_COSE_ALGORITHM_ESP256);
    t_cose_sign1_set_signing_key(&sign_ctx, mechanism_sign.key.cose_key, mechanism_sign.key.kid);
    
    /* encode the header */
    QCBOREncode_Init(&context, buf);
    t_cose_result = t_cose_sign1_encode_parameters(&sign_ctx, &context);
    if (t_cose_result != T_COSE_SUCCESS) {
        result = TEEP_ERR_SIGNING_FAILED;
        goto cleanup;
    }


    /* encoding payload start */
    QCBOREncode_OpenMap(&context);

    /* confirmation */
    //cnf/8: {/ COSE_Key /1:{/kty/1:2, /crv/-1:1, /x/-2:h'...', /y/-3:h'...'},/kid/3:h'...'}
    QCBOREncode_OpenMapInMapN(&context, CNF); // open cnf map
    QCBOREncode_OpenMapInMapN(&context, COSE_KEY); // open cose_key map
    QCBOREncode_AddInt64ToMapN(&context, KEY_TYPE, 2); // key type: EC2
    QCBOREncode_AddInt64ToMapN(&context, CURVE, 1); // curve: P-256
    QCBOREncode_AddBytesToMapN(&context, X_COORDINATE, x_coordinate); // x
    QCBOREncode_AddBytesToMapN(&context, Y_COORDINATE, y_coordinate); // y
    QCBOREncode_CloseMap(&context); // close cose_key map
    QCBOREncode_AddBytesToMapN(&context, KEY_ID, key_pair->kid); // kid
    QCBOREncode_CloseMap(&context); // close cnf map
    
    /* eat_nonce */
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_CHALLENGE) {
        QCBOREncode_AddBytesToMapN(&context, EAT_NONCE, challenge);
    } else {
        QCBOREncode_AddBytesToMapN(&context, EAT_NONCE, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(kDefaultEatNonce));
    }
    /* ueid */
    QCBOREncode_AddBytesToMapN(&context, UEID, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(kUeid));

    /* oemid */
    QCBOREncode_AddBytesToMapN(&context, OEMID, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(kOemid));

    /* hwmodel */
    QCBOREncode_AddBytesToMapN(&context, HWMODEL, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(kHwmodel));

    /* hwversion */
    QCBOREncode_OpenArrayInMapN(&context, HWVERSION);
    QCBOREncode_AddText(&context, UsefulBuf_FROM_SZ_LITERAL("1.3.4"));
    QCBOREncode_AddInt64(&context, 1);
    QCBOREncode_CloseArray(&context);

    /* eat_profile */
    QCBOREncode_AddTextToMapN(&context, EAT_PROFILE, UsefulBuf_FROM_SZ_LITERAL("urn:ietf:rfc:rfc9711"));

    /* measurements */
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
    QCBOREncode_AddBytes(&context, UsefulBuf_FROM_BYTE_ARRAY_LITERAL(kDeadbeefBytes)); //value
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
        result = TEEP_ERR_SIGNING_FAILED;
        goto cleanup;
    }
    
    /* complete CBOR Encoding */
    error = QCBOREncode_Finish(&context, ret);
    if (error != QCBOR_SUCCESS) {
        PRINT_DEBUG_LOG("QCBOREncode_Finish() = %d\n", error);
        result = TEEP_ERR_UNEXPECTED_ERROR;
        goto cleanup;
    }

    result = TEEP_SUCCESS;

cleanup:
    if (result != TEEP_SUCCESS) {
        *ret = NULLUsefulBufC;
    }
    if (key_initialized) {
        teep_free_key(&mechanism_sign.key);
    }
    return result;
}

teep_err_t create_evidence_dcap(const teep_query_request_t *query_request,
                                 UsefulBuf buf,
                                 teep_key_t *key_pair,
                                 UsefulBufC *ret)
{
    sgx_status_t sgx_ret = SGX_SUCCESS;
    teep_err_t result = TEEP_SUCCESS;

    if (query_request == NULL ||
        !is_uncompressed_p256_public_key(key_pair) ||
        ret == NULL ||
        buf.ptr == NULL ||
        buf.len == 0) {
        return TEEP_ERR_INVALID_VALUE;
    }
    *ret = NULLUsefulBufC;

    UsefulBufC challenge = NULLUsefulBufC;
    result = get_query_challenge(query_request, &challenge);
    if (result != TEEP_SUCCESS) {
        return result;
    }

    /* Get QE target info for an application report addressed to the quoting enclave. */
    sgx_target_info_t qe_target_info = {};
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
    if (query_request == NULL ||
        !is_uncompressed_p256_public_key(key_pair) ||
        ret == NULL ||
        buf.ptr == NULL ||
        buf.len == 0) {
        return TEEP_ERR_INVALID_VALUE;
    }
    *ret = NULLUsefulBufC;

    teep_err_t result = TEEP_SUCCESS;
    uint8_t *raw_report_data_buf = NULL;
    UsefulBufC raw_report_data = NULLUsefulBufC;
    UsefulBufC quote = NULLUsefulBufC;
    QCBOREncodeContext context;
    QCBORError error = QCBOR_SUCCESS;

    uint8_t *quote_buf = (uint8_t *)malloc(buf.len);
    if (quote_buf == NULL) {
        return TEEP_ERR_NO_MEMORY;
    }

    UsefulBuf quote_storage = { .ptr = quote_buf, .len = buf.len };
    result = create_evidence_dcap(query_request,
                                  quote_storage,
                                  key_pair,
                                  &quote);
    if (result != TEEP_SUCCESS) {
        goto cleanup;
    }

    result = create_raw_dcap_report_data(query_request,
                                         key_pair,
                                         &raw_report_data_buf,
                                         &raw_report_data);
    if (result != TEEP_SUCCESS) {
        goto cleanup;
    }

    //attestation-payload: << [ raw-dcap-quote3, raw-report-data: x || y || nonce ] >>
    QCBOREncode_Init(&context, buf);
    QCBOREncode_OpenArray(&context);
    QCBOREncode_AddBytes(&context, quote);
    QCBOREncode_AddBytes(&context, raw_report_data);
    QCBOREncode_CloseArray(&context);

    error = QCBOREncode_Finish(&context, ret);
    if (error != QCBOR_SUCCESS) {
        PRINT_DEBUG_LOG("QCBOREncode_Finish() = %d\n", error);
        result = TEEP_ERR_UNEXPECTED_ERROR;
        goto cleanup;
    }

    result = TEEP_SUCCESS;

cleanup:
    if (result != TEEP_SUCCESS) {
        *ret = NULLUsefulBufC;
    }
    free(raw_report_data_buf);
    free(quote_buf);
    return result;
}
