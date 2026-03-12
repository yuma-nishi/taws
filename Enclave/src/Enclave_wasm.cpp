/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>
#include <stdlib.h>

#include "Enclave.h"
#include "debug_print.h"
#include "wasm_export.h"
#include "tc_manager.h"

static bool runtime_inited = false;
// Conservative defaults validated for the current workload.
// TODO: tune with measurement (max stack depth / peak heap) to reduce enclave memory usage.
static const uint32_t wamr_instantiate_stack_size = 32 * 1024 * 1024;
static const uint32_t wamr_instantiate_heap_size = 512 * 1024 * 1024;
static const uint32_t wamr_exec_env_stack_size = 32 * 1024 * 1024;
static uint8_t wamr_global_heap_buf[wamr_instantiate_heap_size];

typedef struct WamrLoadedModule {
    char wapp_name[SUIT_MAX_NAME_LENGTH];
    uint8_t wapp_hash[SHA256_DIGEST_LENGTH];
    UsefulBuf wapp_bin;
    wasm_module_t module;
    wasm_module_inst_t module_instance;
} WamrLoadedModule;

static WamrLoadedModule g_loaded_module = {};

static void
reset_wamr_loaded_module(WamrLoadedModule *loaded_module)
{
    if (loaded_module == NULL) {
        return;
    }
    if (loaded_module->module_instance != NULL) {
        wasm_runtime_deinstantiate(loaded_module->module_instance);
    }
    if (loaded_module->module != NULL) {
        wasm_runtime_unload(loaded_module->module);
    }
    if (loaded_module->wapp_bin.ptr != NULL) {
        free(loaded_module->wapp_bin.ptr);
    }
    memset(loaded_module, 0, sizeof(*loaded_module));
}

static ecall_wasm_result_t
ensure_wamr_running(const char *wapp_name)
{
    if (wapp_name == NULL || wapp_name[0] == '\0') {
        PRINT_DEBUG_LOG("EnsureWamrRunning: wapp_name is empty.\n");
        return ECALL_WASM_RESULT_INVALID_ARGUMENT;
    }

    const manifest_record_t *record = tc_manager_find_record_by_wappname(wapp_name);
    if (record == NULL) {
        PRINT_DEBUG_LOG("EnsureWamrRunning: manifest not found for %s.\n", wapp_name);
        return ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND;
    }
    if (UsefulBuf_IsNULLOrEmpty( record->wapp_bin )) {
        PRINT_DEBUG_LOG("EnsureWamrRunning: wapp_bin not available for %s.\n", wapp_name);
        return ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND;
    }

    WamrLoadedModule *loaded_module = &g_loaded_module;
    if (loaded_module->module_instance != NULL) {
        // Reuse the current module instance if the wapp hash is unchanged;
        // otherwise unload it and reload the updated module.
        if (memcmp(loaded_module->wapp_hash, record->wapp_hash, SHA256_DIGEST_LENGTH) == 0) {
            return ECALL_WASM_RESULT_OK;
        }
        reset_wamr_loaded_module(loaded_module);
    }

    strncpy(loaded_module->wapp_name, wapp_name, sizeof(loaded_module->wapp_name) - 1);

    if (!runtime_inited) {
        RuntimeInitArgs init_args;
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = wamr_global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(wamr_global_heap_buf);
        if (!wasm_runtime_full_init(&init_args)) {
            PRINT_DEBUG_LOG("EnsureWamrRunning: Init runtime environment failed.\n");
            return ECALL_WASM_RESULT_INTERNAL_ERROR;
        }
        runtime_inited = true;
    }

    loaded_module->wapp_bin.ptr = (uint8_t *)malloc(record->wapp_bin.len);
    if (loaded_module->wapp_bin.ptr == NULL) {
        PRINT_DEBUG_LOG("EnsureWamrRunning: wapp_bin allocation failed.\n");
        return ECALL_WASM_RESULT_RESOURCE_EXHAUSTED;
    }
    memcpy(loaded_module->wapp_bin.ptr, record->wapp_bin.ptr, record->wapp_bin.len);
    loaded_module->wapp_bin.len = record->wapp_bin.len;
    char error_buf[128];
    wasm_module_t wasm_module = wasm_runtime_load((uint8_t *)loaded_module->wapp_bin.ptr, (uint32_t)loaded_module->wapp_bin.len, error_buf, sizeof(error_buf));
    if (wasm_module == NULL) {
        PRINT_DEBUG_LOG("EnsureWamrRunning: wasm_runtime_load failed: %s\n", error_buf);
        reset_wamr_loaded_module(loaded_module);
        return ECALL_WASM_RESULT_WASM_INCOMPATIBLE;
    }

    wasm_module_inst_t wasm_module_instance = wasm_runtime_instantiate(
        wasm_module,
        wamr_instantiate_stack_size,
        wamr_instantiate_heap_size,
        error_buf,
        sizeof(error_buf));
    if (wasm_module_instance == NULL) {
        PRINT_DEBUG_LOG("EnsureWamrRunning: wasm_runtime_instantiate failed: %s\n", error_buf);
        wasm_runtime_unload(wasm_module);
        reset_wamr_loaded_module(loaded_module);
        return ECALL_WASM_RESULT_WASM_INCOMPATIBLE;
    }

    loaded_module->module = wasm_module;
    loaded_module->module_instance = wasm_module_instance;
    memcpy(loaded_module->wapp_hash, record->wapp_hash, sizeof(loaded_module->wapp_hash));
    return ECALL_WASM_RESULT_OK;
}

extern "C" ecall_wasm_result_t
ecall_invoke_wasm(const char *wapp_name,
                       const char *func_name,
                       uint8_t *input,
                       size_t input_len,
                       uint8_t *output,
                       size_t output_len,
                       size_t *actual_len)
{
    if (actual_len == NULL) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: actual_len is NULL.\n");
        return ECALL_WASM_RESULT_INVALID_ARGUMENT;
    }
    *actual_len = 0;
    if (wapp_name == NULL || wapp_name[0] == '\0' ||
        func_name == NULL || func_name[0] == '\0' ||
        output == NULL) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: invalid arguments (wapp_name/func_name/output).\n");
        return ECALL_WASM_RESULT_INVALID_ARGUMENT;
    }

    ecall_wasm_result_t ensure_ret = ensure_wamr_running(wapp_name);
    if (ensure_ret != ECALL_WASM_RESULT_OK) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: instance not running for %s.\n", wapp_name);
        return ensure_ret;
    }
    WamrLoadedModule *loaded_module = &g_loaded_module;

    wasm_function_inst_t func = wasm_runtime_lookup_function(loaded_module->module_instance, func_name);
    if (func == NULL) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: function not found: %s\n", func_name);
        return ECALL_WASM_RESULT_WASM_INCOMPATIBLE;
    }

    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(loaded_module->module_instance, wamr_exec_env_stack_size);
    if (exec_env == NULL) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: exec env creation failed.\n");
        return ECALL_WASM_RESULT_INTERNAL_ERROR;
    }

    uint32_t argv[4] = {0};
    uint64_t wasm_in_ptr = 0;
    uint64_t wasm_out_ptr = 0;
    uint8_t *native_in = NULL;
    uint8_t *native_out = NULL;
    uint32_t ret_len = 0;

    if (input_len > 0) {
        wasm_in_ptr = wasm_runtime_module_malloc(loaded_module->module_instance, (uint32_t)input_len, (void **)&native_in);
        if (wasm_in_ptr == 0 || native_in == NULL) {
            PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: wasm_runtime_module_malloc failed (input)\n");
            wasm_runtime_destroy_exec_env(exec_env);
            return ECALL_WASM_RESULT_RESOURCE_EXHAUSTED;
        }
        memcpy(native_in, input, input_len);
    }

    argv[0] = (uint32_t)wasm_in_ptr;
    argv[1] = (uint32_t)input_len;
    argv[2] = 0;
    argv[3] = 0;

    if (!wasm_runtime_call_wasm(exec_env, func, 4, argv)) {
        const char *exception = wasm_runtime_get_exception(loaded_module->module_instance);
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: call failed: %s\n", exception ? exception : "(no exception)");
        if (wasm_in_ptr) {
            wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
        }
        wasm_runtime_destroy_exec_env(exec_env);
        return ECALL_WASM_RESULT_WASM_EXECUTION_FAILED;
    }

    ret_len = argv[0];
    if (ret_len == 0) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: empty output size\n");
        if (wasm_in_ptr) {
            wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
        }
        wasm_runtime_destroy_exec_env(exec_env);
        return ECALL_WASM_RESULT_WASM_EXECUTION_FAILED;
    }
    if (ret_len > output_len) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: output buffer too small (%u > %zu)\n", ret_len, output_len);
        if (wasm_in_ptr) {
            wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
        }
        wasm_runtime_destroy_exec_env(exec_env);
        return ECALL_WASM_RESULT_OUTPUT_BUFFER_TOO_SMALL;
    }

    wasm_out_ptr = wasm_runtime_module_malloc(loaded_module->module_instance, ret_len, (void **)&native_out);
    if (wasm_out_ptr == 0 || native_out == NULL) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: wasm_runtime_module_malloc failed (output)\n");
        if (wasm_in_ptr) {
            wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
        }
        wasm_runtime_destroy_exec_env(exec_env);
        return ECALL_WASM_RESULT_RESOURCE_EXHAUSTED;
    }

    argv[0] = (uint32_t)wasm_in_ptr;
    argv[1] = (uint32_t)input_len;
    argv[2] = (uint32_t)wasm_out_ptr;
    argv[3] = ret_len;

    if (!wasm_runtime_call_wasm(exec_env, func, 4, argv)) {
        const char *exception = wasm_runtime_get_exception(loaded_module->module_instance);
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: call failed (output): %s\n", exception ? exception : "(no exception)");
        wasm_runtime_module_free(loaded_module->module_instance, wasm_out_ptr);
        if (wasm_in_ptr) {
            wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
        }
        wasm_runtime_destroy_exec_env(exec_env);
        return ECALL_WASM_RESULT_WASM_EXECUTION_FAILED;
    }

    uint32_t actual_len_buf = argv[0];
    if (actual_len_buf == 0) {
        PRINT_DEBUG_LOG("Ecall_InvokeWasmBytes: empty output\n");
        wasm_runtime_module_free(loaded_module->module_instance, wasm_out_ptr);
        if (wasm_in_ptr) {
            wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
        }
        wasm_runtime_destroy_exec_env(exec_env);
        return ECALL_WASM_RESULT_WASM_EXECUTION_FAILED;
    }
    if (actual_len_buf > ret_len) {
        actual_len_buf = ret_len;
    }
    memcpy(output, native_out, actual_len_buf);
    *actual_len = actual_len_buf;

    wasm_runtime_module_free(loaded_module->module_instance, wasm_out_ptr);
    if (wasm_in_ptr) {
        wasm_runtime_module_free(loaded_module->module_instance, wasm_in_ptr);
    }
    wasm_runtime_destroy_exec_env(exec_env);
    return ECALL_WASM_RESULT_OK;
}
