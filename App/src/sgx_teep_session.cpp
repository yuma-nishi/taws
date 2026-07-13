/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "sgx_urts.h"
#include "App.h"
#include "Enclave_u.h"
#include "teep_http_client.h"
#include "ecall_process_teep_result.h"
#include "teep_session_result.h"
#include "teep_buffer_sizes.h"
#include "taws_logger.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "qcbor/UsefulBuf.h"
#ifdef __cplusplus
}
#endif

#define MAX_RECEIVE_BUFFER_SIZE         (1024 * 1024 * 20) // 20MB
#define MAX_SEND_BUFFER_SIZE            TEEP_SEND_BUFFER_SIZE

/* Global EID shared by multiple threads */
sgx_enclave_id_t global_eid = 0;

typedef enum {
    TEEP_SEND_LOG_INITIAL_REQUEST,
    TEEP_SEND_LOG_QUERY_RESPONSE,
    TEEP_SEND_LOG_QUERY_RESPONSE_WITH_ATTESTATION,
    TEEP_SEND_LOG_SUCCESS,
    TEEP_SEND_LOG_ERROR
} teep_send_log_type_t;

static void log_teep_send(teep_send_log_type_t type, size_t cose_size)
{
    switch (type) {
        case TEEP_SEND_LOG_INITIAL_REQUEST:
            taws_log_teep_send("sending TEEP initial request: cose_size=%zu", cose_size);
            break;
        case TEEP_SEND_LOG_QUERY_RESPONSE:
            taws_log_teep_send("sending TEEP QueryResponse: cose_size=%zu", cose_size);
            break;
        case TEEP_SEND_LOG_QUERY_RESPONSE_WITH_ATTESTATION:
            taws_log_teep_send("sending TEEP QueryResponse with evidence: cose_size=%zu", cose_size);
            break;
        case TEEP_SEND_LOG_SUCCESS:
            taws_log_teep_send("sending TEEP Success: cose_size=%zu", cose_size);
            break;
        case TEEP_SEND_LOG_ERROR:
            taws_log_teep_send("sending TEEP Error: cose_size=%zu", cose_size);
            break;
    }
}

static teep_send_log_type_t send_log_type_from_ecall_result(ecall_process_teep_result_t result)
{
    switch (result) {
        case ECALL_PROCESS_TEEP_RESULT_RESPONSE_IS_TEEP_ERROR:
            return TEEP_SEND_LOG_ERROR;
        case ECALL_PROCESS_TEEP_RESULT_DEVICE_ACTIVATION_FLOW:
            return TEEP_SEND_LOG_QUERY_RESPONSE_WITH_ATTESTATION;
        case ECALL_PROCESS_TEEP_RESULT_QUERY_RESPONSE:
            return TEEP_SEND_LOG_QUERY_RESPONSE;
        case ECALL_PROCESS_TEEP_RESULT_SUCCESS:
            return TEEP_SEND_LOG_SUCCESS;
        case ECALL_PROCESS_TEEP_RESULT_OK:
        case ECALL_PROCESS_TEEP_RESULT_FATAL:
            break;
    }
    return TEEP_SEND_LOG_SUCCESS;
}

typedef struct _sgx_errlist_t {
    sgx_status_t err;
    const char *msg;
    const char *sug; /* Suggestion */
} sgx_errlist_t;

/* Error code returned by sgx_create_enclave */
static sgx_errlist_t sgx_errlist[] = {
    {
        SGX_ERROR_UNEXPECTED,
        "Unexpected error occurred.",
        NULL
    },
    {
        SGX_ERROR_INVALID_PARAMETER,
        "Invalid parameter.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_MEMORY,
        "Out of memory.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_LOST,
        "Power transition occurred.",
        "Please refer to the sample \"PowerTransition\" for details."
    },
    {
        SGX_ERROR_INVALID_ENCLAVE,
        "Invalid enclave image.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ENCLAVE_ID,
        "Invalid enclave identification.",
        NULL
    },
    {
        SGX_ERROR_INVALID_SIGNATURE,
        "Invalid enclave signature.",
        NULL
    },
    {
        SGX_ERROR_OUT_OF_EPC,
        "Out of EPC memory.",
        NULL
    },
    {
        SGX_ERROR_NO_DEVICE,
        "Invalid SGX device.",
        "Please make sure SGX module is enabled in the BIOS, and install SGX driver afterwards."
    },
    {
        SGX_ERROR_MEMORY_MAP_CONFLICT,
        "Memory map conflicted.",
        NULL
    },
    {
        SGX_ERROR_INVALID_METADATA,
        "Invalid enclave metadata.",
        NULL
    },
    {
        SGX_ERROR_DEVICE_BUSY,
        "SGX device was busy.",
        NULL
    },
    {
        SGX_ERROR_INVALID_VERSION,
        "Enclave version was invalid.",
        NULL
    },
    {
        SGX_ERROR_INVALID_ATTRIBUTE,
        "Enclave was not authorized.",
        NULL
    },
    {
        SGX_ERROR_ENCLAVE_FILE_ACCESS,
        "Can't open enclave file.",
        NULL
    },
    {
        SGX_ERROR_NDEBUG_ENCLAVE,
        "The enclave is signed as product enclave, and can not be created as debuggable enclave.",
        NULL
    },
    {
        SGX_ERROR_MEMORY_MAP_FAILURE,
        "Failed to reserve memory for the enclave.",
        NULL
    },
};

/* Check error conditions for loading enclave */
void print_error_message(sgx_status_t ret)
{
    size_t idx = 0;
    size_t ttl = sizeof sgx_errlist / sizeof sgx_errlist[0];

    for (idx = 0; idx < ttl; idx++) {
        if (ret == sgx_errlist[idx].err) {
            if (sgx_errlist[idx].sug != NULL) {
                TAWS_LOG_INFO("%s", sgx_errlist[idx].sug);
            }
            TAWS_LOG_ERROR("%s", sgx_errlist[idx].msg);
            break;
        }
    }

    if (idx == ttl) {
        TAWS_LOG_ERROR("Unexpected error occurred.");
    }
}

/* Initialize the enclave:
 *   Call sgx_create_enclave to initialize an enclave instance
 */
int initialize_enclave(void)
{
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;

    /* Call sgx_create_enclave to initialize an enclave instance */
    /* Debug Support: set 2nd parameter to 1 */
    ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        print_error_message(ret);
        return -1;
    }

    TAWS_LOG_INFO("enclave initialized");
    return 0;
}

/* OCall functions */
void ocall_print_string(const char *str)
{
    /* Proxy/Bridge will check the length and null-terminate
     * the input string to prevent buffer overflow.
     */
    taws_log_print_enclave_ocall(str);
}

teep_session_result_t run_teep_session(const char *tam_url, const char *app_name)
{
    static uint8_t cbor_recv_storage[MAX_RECEIVE_BUFFER_SIZE];
    UsefulBuf cbor_recv_buf = (UsefulBuf){ .ptr = cbor_recv_storage, .len = MAX_RECEIVE_BUFFER_SIZE };
    UsefulBuf_MAKE_STACK_UB(cose_send_buf, MAX_SEND_BUFFER_SIZE);

    cose_send_buf.len = 0; /* first message is NULL on teep over http */
    bool has_teep_error_response = false;
    bool in_device_activation_flow = false;
    teep_send_log_type_t send_log_type = TEEP_SEND_LOG_INITIAL_REQUEST;

    TAWS_LOG_INFO("TEEP install session started");
    while (1) {
        cbor_recv_buf.len = MAX_RECEIVE_BUFFER_SIZE;
        log_teep_send(send_log_type, cose_send_buf.len);
        int result = teep_send_http_post(tam_url, UsefulBuf_Const(cose_send_buf), &cbor_recv_buf);
        if (result != 0) {
            return TEEP_SESSION_RESULT_HTTP_ERROR;
        }
        if (cbor_recv_buf.len == 0) {
            // No response body (e.g., HTTP 204). Treat as successful end of session.
            break;
        }

        ecall_process_teep_result_t ret = ECALL_PROCESS_TEEP_RESULT_OK;
        size_t cose_send_len = MAX_SEND_BUFFER_SIZE;
        TAWS_LOG_DEBUG("ECall ecall_process_message send_capacity=%zu recv_len=%zu",
               (size_t)MAX_SEND_BUFFER_SIZE,
               cbor_recv_buf.len);
        sgx_status_t sgx_ret = ecall_process_message(global_eid,
                                                     &ret,
                                                     (uint8_t *)cbor_recv_buf.ptr,
                                                     cbor_recv_buf.len,
                                                     app_name,
                                                     (uint8_t *)cose_send_buf.ptr,
                                                     MAX_SEND_BUFFER_SIZE,
                                                     &cose_send_len);
        if (sgx_ret != SGX_SUCCESS) {
            print_error_message(sgx_ret);
            return TEEP_SESSION_RESULT_FATAL;
        }
        if (ret == ECALL_PROCESS_TEEP_RESULT_FATAL) {
            TAWS_LOG_ERROR("ecall_process_message failed: %d", ret);
            return TEEP_SESSION_RESULT_FATAL;
        }
        if (ret == ECALL_PROCESS_TEEP_RESULT_RESPONSE_IS_TEEP_ERROR) {
            has_teep_error_response = true;
        }
        if (ret == ECALL_PROCESS_TEEP_RESULT_DEVICE_ACTIVATION_FLOW) {
            in_device_activation_flow = true;
        }
        cose_send_buf.len = cose_send_len;
        send_log_type = send_log_type_from_ecall_result(ret);

        sleep(1);
    }

    if (has_teep_error_response) {
        TAWS_LOG_ERROR("TEEP install session finished with TEEP error response");
        return TEEP_SESSION_RESULT_TEEP_ERROR_RESPONSE;
    }
    if (in_device_activation_flow) {
        TAWS_LOG_INFO("TEEP install session finished: device activation flow");
        return TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED;
    }
    TAWS_LOG_INFO("TEEP install session finished");
    return TEEP_SESSION_RESULT_OK;
}
