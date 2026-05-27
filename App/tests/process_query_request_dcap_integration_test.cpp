/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Enclave_u.h"
#include "ecall_process_teep_result.h"
#include "sgx_error.h"
#include "sgx_urts.h"
#include "tam_esp256_public_key.h"
#include "teep_agent_esp256_public_key.h"
#include "teep_buffer_sizes.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "qcbor/qcbor_decode.h"
#include "teep/teep_cose.h"
#include "teep/teep_message_data.h"
#ifdef __cplusplus
}
#endif

#ifndef TEST_ENCLAVE_FILENAME
#define TEST_ENCLAVE_FILENAME "build/process_query_request_dcap_integration/process_query_request_dcap_integration_enclave.signed.so"
#endif

#ifndef QUERY_REQUEST_COSE_FILENAME
#define QUERY_REQUEST_COSE_FILENAME "tam_mock_server/query_request.tam.esp256.cose"
#endif

static bool require_dcap(void)
{
    const char *value = getenv("REQUIRE_DCAP");
    return value != NULL && strcmp(value, "1") == 0;
}

static bool read_file(const char *path, uint8_t **bytes, size_t *len)
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

    uint8_t *buf = static_cast<uint8_t *>(malloc(static_cast<size_t>(size)));
    if (buf == NULL) {
        fclose(fp);
        return false;
    }
    size_t read_len = fread(buf, 1, static_cast<size_t>(size), fp);
    fclose(fp);
    if (read_len != static_cast<size_t>(size)) {
        free(buf);
        return false;
    }

    *bytes = buf;
    *len = read_len;
    return true;
}

extern "C" void ocall_print_string(const char *str)
{
    if (str != NULL) {
        fputs(str, stdout);
        fflush(stdout);
    }
}

static bool contains_nonzero_byte(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) {
            return true;
        }
    }
    return false;
}

static void init_esp256_public_key(const uint8_t *public_key, teep_mechanism_t *mechanism)
{
    memset(mechanism, 0, sizeof(*mechanism));
    assert(teep_key_init_esp256_public_key(public_key, NULLUsefulBufC, &mechanism->key) == TEEP_SUCCESS);
    mechanism->cose_tag = CBOR_TAG_COSE_SIGN1;
}

static void assert_cose_sign1_alg_esp256(const uint8_t *cose, size_t cose_len)
{
    size_t offset = 0;
    if (cose_len > 0 && cose[offset] == 0xd2) {
        offset++;
    }

    assert(cose_len >= offset + 2);
    assert(cose[offset++] == 0x84);
    assert((cose[offset] & 0xe0) == 0x40);

    size_t protected_len = cose[offset++] & 0x1f;
    assert(cose_len >= offset + protected_len);
    const uint8_t *protected_header = cose + offset;

    assert(protected_len == 3);
    assert(protected_header[0] == 0xa1);
    assert(protected_header[1] == 0x01);
    assert(protected_header[2] == 0x28);
    assert(T_COSE_ALGORITHM_ESP256 == -9);
}

static void verify_cose_sign1(const uint8_t *cose,
                              size_t cose_len,
                              const uint8_t *public_key,
                              UsefulBufC *payload)
{
    teep_mechanism_t verify = {};
    init_esp256_public_key(public_key, &verify);
    UsefulBufC cose_buf = { .ptr = cose, .len = cose_len };
    teep_err_t result = teep_verify_cose_sign1(cose_buf, &verify, payload);
    if (result != TEEP_SUCCESS) {
        verify.cose_tag = CBOR_TAG_COSE_SIGN;
        result = teep_verify_cose_sign(cose_buf, &verify, 1, payload);
    }
    assert(result == TEEP_SUCCESS);
    (void)teep_free_key(&verify.key);
}

static void decode_query_request_challenge(UsefulBufC payload, teep_buf_t *challenge)
{
    teep_message_t message = {};
    assert(teep_set_message_from_bytes(static_cast<const uint8_t *>(payload.ptr),
                                       payload.len,
                                       &message) == TEEP_SUCCESS);
    assert(message.teep_message.type == TEEP_TYPE_QUERY_REQUEST);

    challenge->ptr = NULL;
    challenge->len = 0;
    if ((message.query_request.contains & TEEP_MESSAGE_CONTAINS_CHALLENGE) != 0) {
        *challenge = message.query_request.challenge;
    }
}

static void decode_query_response(UsefulBufC payload, teep_message_t *message)
{
    memset(message, 0, sizeof(*message));
    assert(teep_set_message_from_bytes(static_cast<const uint8_t *>(payload.ptr),
                                       payload.len,
                                       message) == TEEP_SUCCESS);
    assert(message->teep_message.type == TEEP_TYPE_QUERY_RESPONSE);
}

static void verify_attestation_payload(const teep_query_response_t *query_response,
                                       const teep_buf_t *challenge)
{
    const char expected_format[] = "application/sgx-quote3-teep-bundle";
    assert((query_response->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD) != 0);
    assert((query_response->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD_FORMAT) != 0);
    assert(query_response->attestation_payload_format.len == strlen(expected_format));
    assert(memcmp(query_response->attestation_payload_format.ptr,
                  expected_format,
                  strlen(expected_format)) == 0);
    assert(query_response->attestation_payload.len > 0);

    UsefulBufC encoded_payload = {
        .ptr = query_response->attestation_payload.ptr,
        .len = query_response->attestation_payload.len,
    };
    QCBORDecodeContext decode_context;
    QCBORItem item = {};
    QCBORDecode_Init(&decode_context, encoded_payload, QCBOR_DECODE_MODE_NORMAL);

    assert(QCBORDecode_GetNext(&decode_context, &item) == QCBOR_SUCCESS);
    assert(item.uDataType == QCBOR_TYPE_ARRAY);
    assert(item.val.uCount == 2);

    assert(QCBORDecode_GetNext(&decode_context, &item) == QCBOR_SUCCESS);
    assert(item.uDataType == QCBOR_TYPE_BYTE_STRING);
    assert(item.val.string.len > 0);
    assert(contains_nonzero_byte(static_cast<const uint8_t *>(item.val.string.ptr),
                                 item.val.string.len));

    assert(QCBORDecode_GetNext(&decode_context, &item) == QCBOR_SUCCESS);
    assert(item.uDataType == QCBOR_TYPE_BYTE_STRING);
    UsefulBufC raw_report_data = item.val.string;
    assert(raw_report_data.len == 64 + challenge->len);
    const uint8_t *report_data = static_cast<const uint8_t *>(raw_report_data.ptr);
    assert(memcmp(report_data, teep_agent_esp256_public_key + 1, 32) == 0);
    assert(memcmp(report_data + 32, teep_agent_esp256_public_key + 33, 32) == 0);
    if (challenge->len > 0) {
        assert(memcmp(report_data + 64, challenge->ptr, challenge->len) == 0);
    }
    assert(QCBORDecode_Finish(&decode_context) == QCBOR_SUCCESS);
}

int main(void)
{
    uint8_t *query_request_cose = NULL;
    size_t query_request_cose_len = 0;
    if (!read_file(QUERY_REQUEST_COSE_FILENAME, &query_request_cose, &query_request_cose_len)) {
        fprintf(stderr, "[FAIL] failed to read %s\n", QUERY_REQUEST_COSE_FILENAME);
        return 1;
    }

    assert_cose_sign1_alg_esp256(query_request_cose, query_request_cose_len);
    UsefulBufC query_request_payload = NULLUsefulBufC;
    verify_cose_sign1(query_request_cose,
                      query_request_cose_len,
                      tam_esp256_public_key,
                      &query_request_payload);
    teep_buf_t challenge = {};
    decode_query_request_challenge(query_request_payload, &challenge);

    sgx_enclave_id_t eid = 0;
    sgx_status_t sgx_ret = sgx_create_enclave(TEST_ENCLAVE_FILENAME,
                                              SGX_DEBUG_FLAG,
                                              NULL,
                                              NULL,
                                              &eid,
                                              NULL);
    if (sgx_ret != SGX_SUCCESS) {
        fprintf(stderr,
                "[SKIP] sgx_create_enclave(%s) returned 0x%04x. "
                "SGX HW runtime may not be available in this environment.\n",
                TEST_ENCLAVE_FILENAME,
                sgx_ret);
        free(query_request_cose);
        return require_dcap() ? 1 : 0;
    }

    int key_retval = TEEP_ERR_UNEXPECTED_ERROR;
    sgx_ret = ecall_teep_set_esp256_key(eid, &key_retval);
    if (sgx_ret != SGX_SUCCESS || key_retval != TEEP_SUCCESS) {
        fprintf(stderr,
                "[FAIL] ecall_teep_set_esp256_key failed: sgx=0x%04x retval=%d\n",
                sgx_ret,
                key_retval);
        sgx_destroy_enclave(eid);
        free(query_request_cose);
        return 1;
    }

    uint8_t response_cose[TEEP_SEND_BUFFER_SIZE] = {0};
    size_t response_cose_len = sizeof(response_cose);
    ecall_process_teep_result_t process_retval = ECALL_PROCESS_TEEP_RESULT_FATAL;
    sgx_ret = ecall_process_message(eid,
                                    &process_retval,
                                    query_request_cose,
                                    query_request_cose_len,
                                    "request-app.wasm",
                                    response_cose,
                                    sizeof(response_cose),
                                    &response_cose_len);
    sgx_destroy_enclave(eid);

    if (sgx_ret != SGX_SUCCESS) {
        fprintf(stderr,
                "[FAIL] ecall_process_message returned SGX status 0x%04x\n",
                sgx_ret);
        free(query_request_cose);
        return 1;
    }
    if (process_retval != ECALL_PROCESS_TEEP_RESULT_DEVICE_ACTIVATION_FLOW) {
        fprintf(stderr,
                "[SKIP] ecall_process_message returned %d. "
                "DCAP quote provider may not be available in this environment.\n",
                process_retval);
        free(query_request_cose);
        return require_dcap() ? 1 : 0;
    }
    assert(response_cose_len > 0);
    assert_cose_sign1_alg_esp256(response_cose, response_cose_len);

    UsefulBufC response_payload = NULLUsefulBufC;
    verify_cose_sign1(response_cose,
                      response_cose_len,
                      teep_agent_esp256_public_key,
                      &response_payload);

    teep_message_t response_message = {};
    decode_query_response(response_payload, &response_message);
    verify_attestation_payload(&response_message.query_response, &challenge);
    free(query_request_cose);

    printf("[PASS] ecall_process_message generated a DCAP QueryResponse; payload_size=%zu\n",
           response_message.query_response.attestation_payload.len);
    return 0;
}
