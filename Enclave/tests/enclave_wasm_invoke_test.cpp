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

#include "Enclave.h"
#include "tc_manager.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "wasm_export.h"
#ifdef __cplusplus
}
#undef delete
#endif

extern "C" ecall_wasm_result_t ecall_invoke_wasm(const char *wapp_name,
                                                  const char *func_name,
                                                  uint8_t *input,
                                                  size_t input_len,
                                                  uint8_t *output,
                                                  size_t output_len,
                                                  size_t *actual_len);

/*
 * Test purpose:
 * - Verify control flow and return-value contracts of ecall_invoke_wasm().
 * - Verify argument checks and output-size handling in the ECALL logic.
 * - Do not validate WAMR runtime implementation itself.
 *
 * Approach:
 * - tc_manager and wasm_runtime_* APIs are stubbed in this file.
 * - The test controls stub behavior to exercise ECALL branches deterministically.
 */
static manifest_record_t g_tc_manager_record = {};
static uint8_t g_record_payload[] = {0x00, 0x01, 0x02, 0x03};
static const char *k_test_wapp_name = "app";
static const uint8_t k_test_wapp_hash_seed = 0x11;

static bool g_test_lookup_function_should_fail = false;
static bool g_test_create_exec_env_should_fail = false;
static bool g_test_first_call_should_fail = false;
static bool g_test_second_call_should_fail = false;
static bool g_test_input_module_malloc_should_fail = false;
static bool g_test_output_module_malloc_should_fail = false;
static bool g_test_module_load_should_fail = false;
static bool g_test_runtime_full_init_should_fail = false;
static bool g_test_module_instantiate_should_fail = false;
static uint32_t g_test_first_call_returned_output_size = 0;
static uint32_t g_test_second_call_actual_output_size = 0;
static const char *g_test_second_call_output_bytes = "abc";
static int g_test_wasm_call_phase = 0;
static uint8_t *g_test_native_output_buffer = NULL;

static void test_helper_set_default_wasm_stub_state(void)
{
    memset(&g_tc_manager_record, 0, sizeof(g_tc_manager_record));
    strcpy(g_tc_manager_record.wapp_name, k_test_wapp_name);
    memset(g_tc_manager_record.wapp_hash, k_test_wapp_hash_seed, sizeof(g_tc_manager_record.wapp_hash));
    g_tc_manager_record.wapp_bin.ptr = g_record_payload;
    g_tc_manager_record.wapp_bin.len = sizeof(g_record_payload);

    g_test_lookup_function_should_fail = false;
    g_test_create_exec_env_should_fail = false;
    g_test_first_call_should_fail = false;
    g_test_second_call_should_fail = false;
    g_test_input_module_malloc_should_fail = false;
    g_test_output_module_malloc_should_fail = false;
    g_test_module_load_should_fail = false;
    g_test_runtime_full_init_should_fail = false;
    g_test_module_instantiate_should_fail = false;
    g_test_first_call_returned_output_size = 0;
    g_test_second_call_actual_output_size = 0;
    g_test_second_call_output_bytes = "abc";
    g_test_wasm_call_phase = 0;
    g_test_native_output_buffer = NULL;
}

extern "C" const manifest_record_t *tc_manager_find_record_by_wappname(const char *wapp_name)
{
    if (wapp_name == NULL) {
        return NULL;
    }
    if (strcmp(wapp_name, g_tc_manager_record.wapp_name) == 0) {
        return &g_tc_manager_record;
    }
    return NULL;
}

extern "C" bool wasm_runtime_full_init(RuntimeInitArgs *init_args)
{
    (void)init_args;
    if (g_test_runtime_full_init_should_fail) {
        return false;
    }
    return true;
}

extern "C" wasm_module_t wasm_runtime_load(uint8_t *buf, uint32_t size, char *error_buf, uint32_t error_buf_size)
{
    (void)buf;
    (void)size;
    (void)error_buf;
    (void)error_buf_size;
    if (g_test_module_load_should_fail) {
        return NULL;
    }
    return (wasm_module_t)0x1001;
}

extern "C" wasm_module_inst_t wasm_runtime_instantiate(const wasm_module_t module,
                                                       uint32_t default_stack_size,
                                                       uint32_t host_managed_heap_size,
                                                       char *error_buf,
                                                       uint32_t error_buf_size)
{
    (void)module;
    (void)default_stack_size;
    (void)host_managed_heap_size;
    (void)error_buf;
    (void)error_buf_size;
    if (g_test_module_instantiate_should_fail) {
        return NULL;
    }
    return (wasm_module_inst_t)0x2001;
}

extern "C" void wasm_runtime_unload(wasm_module_t module)
{
    (void)module;
}

extern "C" void wasm_runtime_deinstantiate(wasm_module_inst_t module_inst)
{
    (void)module_inst;
}

extern "C" wasm_function_inst_t wasm_runtime_lookup_function(const wasm_module_inst_t module_inst,
                                                             const char *name)
{
    (void)module_inst;
    if (g_test_lookup_function_should_fail || name == NULL || strcmp(name, "detect") != 0) {
        return NULL;
    }
    return (wasm_function_inst_t)0x3001;
}

extern "C" wasm_exec_env_t wasm_runtime_create_exec_env(wasm_module_inst_t module_inst, uint32_t stack_size)
{
    (void)module_inst;
    (void)stack_size;
    if (g_test_create_exec_env_should_fail) {
        return NULL;
    }
    return (wasm_exec_env_t)0x4001;
}

extern "C" void wasm_runtime_destroy_exec_env(wasm_exec_env_t exec_env)
{
    (void)exec_env;
}

extern "C" uint64_t wasm_runtime_module_malloc(wasm_module_inst_t module_inst, uint64_t size, void **p_native_addr)
{
    (void)module_inst;
    if (size == 0 || p_native_addr == NULL) {
        return 0;
    }
    if (g_test_wasm_call_phase == 0 && g_test_input_module_malloc_should_fail) {
        *p_native_addr = NULL;
        return 0;
    }
    if (g_test_wasm_call_phase == 1 && g_test_output_module_malloc_should_fail) {
        *p_native_addr = NULL;
        return 0;
    }
    void *p = malloc((size_t)size);
    if (p == NULL) {
        *p_native_addr = NULL;
        return 0;
    }
    *p_native_addr = p;
    if (g_test_wasm_call_phase == 1) {
        g_test_native_output_buffer = (uint8_t *)p;
        return 0xA002;
    }
    return 0xA001;
}

extern "C" void wasm_runtime_module_free(wasm_module_inst_t module_inst, uint64_t ptr)
{
    (void)module_inst;
    (void)ptr;
}

extern "C" bool wasm_runtime_call_wasm(wasm_exec_env_t exec_env, wasm_function_inst_t function, uint32_t argc, uint32_t argv[])
{
    (void)exec_env;
    (void)function;
    (void)argc;

    if (g_test_wasm_call_phase == 0) {
        g_test_wasm_call_phase++;
        if (g_test_first_call_should_fail) {
            return false;
        }
        argv[0] = g_test_first_call_returned_output_size;
        return true;
    }

    g_test_wasm_call_phase++;
    if (g_test_second_call_should_fail) {
        return false;
    }
    if (g_test_native_output_buffer != NULL && g_test_second_call_actual_output_size > 0) {
        memcpy(g_test_native_output_buffer, g_test_second_call_output_bytes, g_test_second_call_actual_output_size);
    }
    argv[0] = g_test_second_call_actual_output_size;
    return true;
}

extern "C" const char *wasm_runtime_get_exception(wasm_module_inst_t module_inst)
{
    (void)module_inst;
    return "stub exception";
}

static void test_returns_error_when_required_args_are_invalid(void)
{
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm(NULL, "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_INVALID_ARGUMENT);
}

static void test_returns_error_when_runtime_initialization_fails_while_ensuring_runtime_state(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_runtime_full_init_should_fail = true;

    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_INTERNAL_ERROR);
}

static void test_returns_error_when_manifest_record_for_target_wapp_is_not_found(void)
{
    test_helper_set_default_wasm_stub_state();
    strcpy(g_tc_manager_record.wapp_name, "another_app");

    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND);
}

static void test_returns_error_when_target_wapp_record_has_empty_wapp_binary(void)
{
    test_helper_set_default_wasm_stub_state();
    g_tc_manager_record.wapp_bin.ptr = NULL;
    g_tc_manager_record.wapp_bin.len = 0;

    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND);
}

static void test_returns_error_when_wasm_module_load_fails_while_ensuring_runtime_state(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_module_load_should_fail = true;
    /* g_loaded_module in Enclave_wasm.cpp may keep state across tests.
     * Set a different hash to force reload path (not reuse path), so this test
     * reliably reaches wasm_runtime_load() failure handling. */
    memset(g_tc_manager_record.wapp_hash, 0x22, sizeof(g_tc_manager_record.wapp_hash));

    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_WASM_INCOMPATIBLE);
}

static void test_returns_error_when_wasm_module_instantiation_fails_while_ensuring_runtime_state(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_module_instantiate_should_fail = true;

    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_WASM_INCOMPATIBLE);
}

static void test_returns_error_when_target_wasm_function_is_not_found(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_lookup_function_should_fail = true;
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_WASM_INCOMPATIBLE);
}

static void test_returns_error_when_exec_env_creation_fails(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_create_exec_env_should_fail = true;
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_INTERNAL_ERROR);
}

static void test_returns_error_when_first_wasm_call_for_output_size_fails(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_first_call_should_fail = true;
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_WASM_EXECUTION_FAILED);
}

static void test_returns_error_when_input_buffer_allocation_in_wasm_memory_fails(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_input_module_malloc_should_fail = true;
    uint8_t input[4] = {1, 2, 3, 4};
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", input, sizeof(input), out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_RESOURCE_EXHAUSTED);
}

static void test_returns_error_when_required_output_size_exceeds_host_output_buffer(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_first_call_returned_output_size = 10;
    uint8_t out[4] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_OUTPUT_BUFFER_TOO_SMALL);
}

static void test_returns_error_when_output_buffer_allocation_in_wasm_memory_fails(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_first_call_returned_output_size = 3;
    g_test_output_module_malloc_should_fail = true;
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_RESOURCE_EXHAUSTED);
}

static void test_returns_error_when_second_wasm_call_for_output_bytes_fails(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_first_call_returned_output_size = 3;
    g_test_second_call_should_fail = true;
    uint8_t out[8] = {};
    size_t actual = 999;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_WASM_EXECUTION_FAILED);
}

static void test_returns_success_and_copies_output_when_all_wasm_steps_succeed(void)
{
    test_helper_set_default_wasm_stub_state();
    g_test_first_call_returned_output_size = 3;
    g_test_second_call_actual_output_size = 3;
    g_test_second_call_output_bytes = "xyz";

    uint8_t out[8] = {};
    size_t actual = 0;
    int ret = ecall_invoke_wasm("app", "detect", NULL, 0, out, sizeof(out), &actual);
    assert(ret == ECALL_WASM_RESULT_OK);
    assert(memcmp(out, g_test_second_call_output_bytes, 3) == 0);
    assert(actual == g_test_second_call_actual_output_size);

}

int main(void)
{
    test_returns_error_when_required_args_are_invalid();
    printf("[PASS] 1/14 invalid args\n");
    test_returns_error_when_runtime_initialization_fails_while_ensuring_runtime_state();
    printf("[PASS] 2/14 runtime init failure during ensure\n");
    test_returns_error_when_manifest_record_for_target_wapp_is_not_found();
    printf("[PASS] 3/14 manifest not found\n");
    test_returns_error_when_target_wapp_record_has_empty_wapp_binary();
    printf("[PASS] 4/14 wapp binary empty\n");
    test_returns_error_when_wasm_module_load_fails_while_ensuring_runtime_state();
    printf("[PASS] 5/14 module load failure during ensure\n");
    test_returns_error_when_wasm_module_instantiation_fails_while_ensuring_runtime_state();
    printf("[PASS] 6/14 module instantiate failure during ensure\n");
    test_returns_error_when_target_wasm_function_is_not_found();
    printf("[PASS] 7/14 function not found\n");
    test_returns_error_when_exec_env_creation_fails();
    printf("[PASS] 8/14 create exec env failure\n");
    test_returns_error_when_first_wasm_call_for_output_size_fails();
    printf("[PASS] 9/14 first call wasm failure\n");
    test_returns_error_when_input_buffer_allocation_in_wasm_memory_fails();
    printf("[PASS] 10/14 input module malloc failure\n");
    test_returns_error_when_required_output_size_exceeds_host_output_buffer();
    printf("[PASS] 11/14 output buffer too small\n");
    test_returns_error_when_output_buffer_allocation_in_wasm_memory_fails();
    printf("[PASS] 12/14 output module malloc failure\n");
    test_returns_error_when_second_wasm_call_for_output_bytes_fails();
    printf("[PASS] 13/14 second call wasm failure\n");
    test_returns_success_and_copies_output_when_all_wasm_steps_succeed();
    printf("[PASS] 14/14 success path\n");
    return 0;
}
