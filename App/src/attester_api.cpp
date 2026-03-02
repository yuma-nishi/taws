/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "attester_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "App.h"
#include "Enclave_u.h"
#include "sgx_urts.h"

int initialize_enclave(void);
void print_error_message(sgx_status_t ret);
teep_session_result_t run_teep_session(const char *tam_url, const char *app_name);

static bool g_initialized = false;
static bool g_key_ready = false;
static int sgx_initialized(const char *keygen_mode)
{
    if (g_initialized) {
        return 0;
    }
    if (initialize_enclave() < 0) {
        return -1;
    }
    g_initialized = true;

    if (keygen_mode == NULL || strcmp(keygen_mode, "") == 0) {
        keygen_mode = "yes";
    }

    if (!g_key_ready) {
        int result = 0;
        sgx_status_t sgx_ret = SGX_SUCCESS;
        if (strcmp(keygen_mode, "yes") == 0) {
            sgx_ret = ecall_teep_generate_es256_key_pair(global_eid, &result);
        } else if (strcmp(keygen_mode, "no") == 0) {
            sgx_ret = ecall_teep_set_es256_key(global_eid, &result);
        } else {
            return -1;
        }
        if (sgx_ret != SGX_SUCCESS) {
            print_error_message(sgx_ret);
            return -1;
        }
        if (result != 0) {
            return result;
        }
        g_key_ready = true;
    }

    return 0;
}

int attester_init(const char *keygen_mode)
{
    return sgx_initialized(keygen_mode);
}

teep_session_result_t attester_install(const char *tam_url, const char *app_name)
{
    int init = sgx_initialized(NULL);
    if (init != 0) {
        return TEEP_SESSION_RESULT_FATAL;
    }
    if (tam_url == NULL || app_name == NULL) {
        return TEEP_SESSION_RESULT_FATAL;
    }
    return run_teep_session(tam_url, app_name);
}

int attester_invoke_wasm(const char *wapp_name,
                               const char *func_name,
                               const uint8_t *input,
                               size_t input_len,
                               uint8_t *output,
                               size_t output_len,
                               size_t *actual_len)
{
    int init = sgx_initialized(NULL);
    if (init != 0) {
        return init;
    }
    if (wapp_name == NULL || wapp_name[0] == '\0' ||
        func_name == NULL || func_name[0] == '\0' ||
        output == NULL || actual_len == NULL) {
        return -1;
    }
    ecall_wasm_result_t result = ECALL_WASM_RESULT_OK;
    sgx_status_t sgx_ret = ecall_invoke_wasm(global_eid,
                                                   &result,
                                                   wapp_name,
                                                   func_name,
                                                   const_cast<uint8_t *>(input),
                                                   input_len,
                                                   output,
                                                   output_len,
                                                   actual_len);
    if (sgx_ret != SGX_SUCCESS) {
        print_error_message(sgx_ret);
        return -1;
    }
    return (int)result;
}

void attester_close(void)
{
    if (g_initialized) {
        sgx_destroy_enclave(global_eid);
        g_initialized = false;
        g_key_ready = false;
    }
}
