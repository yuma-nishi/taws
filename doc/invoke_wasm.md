# invoke_wasm Design

## 1. Purpose
This document clarifies the responsibilities, input/output contract, major failure conditions, and test scope of `ecall_invoke_wasm`.

## 2. Scope
- Target implementation: `Enclave/src/Enclave_wasm.cpp`
- Related definition: `common/ecall_wasm_result.h`
- Caller implementation: `App/src/attester_api.cpp` (`attester_invoke_wasm`)

## 3. Module Overview
This module provides an ECALL that runs a WASM binary stored in the TEE by using WAMR.

Main characteristics:
- A single ECALL (`ecall_invoke_wasm`) completes both runtime preparation and WASM function execution.
- It switches between reusing an existing instance and reloading a module based on `wapp_hash`.
- It uses `ecall_wasm_result_t` as a unified return type so the caller can identify failure type.

## 4. Interface
### 4.1 Public Function
```c
ecall_wasm_result_t ecall_invoke_wasm(const char *wapp_name,
                                      const char *func_name,
                                      uint8_t *input,
                                      size_t input_len,
                                      uint8_t *output,
                                      size_t output_len,
                                      size_t *actual_len);
```

### 4.2 Argument Specification
| Argument | In/Out | Description |
| --- | --- | --- |
| `wapp_name` | in | Target WASM app name. Lookup key for records in `tc_manager`. |
| `func_name` | in | Name of the WASM function to call. |
| `input` | in | Input data passed to the WASM function. Unused when `input_len == 0`. |
| `input_len` | in | Length of `input` in bytes. |
| `output` | out | Output buffer allocated by the caller. |
| `output_len` | in | Capacity of `output` in bytes. |
| `actual_len` | out | Actual bytes written to `output`. Initialized to `0` at function start. |

### 4.3 Return Values
`ecall_invoke_wasm` returns `ecall_wasm_result_t`.

| Value | Meaning | Notes |
| --- | --- | --- |
| `ECALL_WASM_RESULT_OK` | Success | None |
| `ECALL_WASM_RESULT_INVALID_ARGUMENT` | Invalid argument | Review arguments and retry |
| `ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND` | Trusted Component is not available | Check install/update status of target `wapp`, or verify `wapp` name |
| `ECALL_WASM_RESULT_RESOURCE_EXHAUSTED` | Failed due to resource shortage | Treat as temporary failure and retry after reducing load |
| `ECALL_WASM_RESULT_WASM_INCOMPATIBLE` | WASM binary is incompatible | Reinstall or update the target `wapp` |
| `ECALL_WASM_RESULT_WASM_EXECUTION_FAILED` | WASM execution failed | Retry execution, or consider reinstall/update of `wapp` |
| `ECALL_WASM_RESULT_INTERNAL_ERROR` | Internal processing failed | Treat as internal bug or unexpected failure and stop processing |
| `ECALL_WASM_RESULT_OUTPUT_BUFFER_TOO_SMALL` | Output buffer is too small | Increase `output_len` and retry |

## 5. Processing Specification
### 5.1 Pre-execution Preparation (`ensure_wamr_running`)
Before function execution, `ecall_invoke_wasm` calls `ensure_wamr_running` to ensure the target app is executable.

Key steps:
1. Find a record by `wapp_name` and check whether `wapp_bin` exists.
2. If an existing instance is present and `wapp_hash` matches, reuse it.
3. If `wapp_hash` does not match, discard the existing instance and reload.
4. Run `wasm_runtime_full_init` only when runtime is not initialized.
5. Copy `wapp_bin` into internal memory, then run `wasm_runtime_load` and `wasm_runtime_instantiate`.

### 5.2 WASM Invocation (Two-phase)
`ecall_invoke_wasm` calls the same WASM function twice.

- First call: get required output size.
- Second call: pass output buffer and get actual output bytes.

For this reason, the caller must pre-allocate and pass `output` and `output_len`.

### 5.3 Resource Management
- Execution environment created by `wasm_runtime_create_exec_env` is always released on both success and failure.
- Input/output buffers allocated in WASM memory are freed only when allocation succeeded.
- On reload, `reset_wamr_loaded_module` frees old instance, module, and copied `wapp_bin`.

## 6. WAMR Execution Context
This module keeps WAMR initialization state and currently loaded module information in global state.
The following data is referenced/updated during `ecall_invoke_wasm`.

| Item | Type | Description |
| --- | --- | --- |
| `runtime_inited` | `bool` | Flag indicating whether WAMR runtime is initialized |
| `g_loaded_module.wapp_name` | `char[]` | Name of currently loaded app |
| `g_loaded_module.wapp_hash` | `uint8_t[SHA256_DIGEST_LENGTH]` | Hash of currently loaded app |
| `g_loaded_module.wapp_bin` | `UsefulBuf` | Copied WASM binary currently loaded |
| `g_loaded_module.module` | `wasm_module_t` | Loaded module |
| `g_loaded_module.module_instance` | `wasm_module_inst_t` | Instantiated module for execution |

## 7. Design Assumptions and Constraints
- Only one loaded module is kept at a time (`g_loaded_module`).
- The same `wapp_hash` is reused without reloading.
- Stack and heap sizes are fixed constants (defined in `Enclave_wasm.cpp`); dynamic tuning is not implemented.

## 8. Test
### 8.1 Unit Test
Target file: `Enclave/tests/enclave_wasm_invoke_test.cpp`

| Test item | Covered case |
| --- | --- |
| `ECALL_WASM_RESULT_INVALID_ARGUMENT` | `test_returns_error_when_required_args_are_invalid` |
| `ECALL_WASM_RESULT_INTERNAL_ERROR` | `test_returns_error_when_runtime_initialization_fails_while_ensuring_runtime_state`, `test_returns_error_when_exec_env_creation_fails` |
| `ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND` | `test_returns_error_when_manifest_record_for_target_wapp_is_not_found`, `test_returns_error_when_target_wapp_record_has_empty_wapp_binary` |
| `ECALL_WASM_RESULT_WASM_INCOMPATIBLE` | `test_returns_error_when_wasm_module_load_fails_while_ensuring_runtime_state`, `test_returns_error_when_wasm_module_instantiation_fails_while_ensuring_runtime_state`, `test_returns_error_when_target_wasm_function_is_not_found` |
| `ECALL_WASM_RESULT_WASM_EXECUTION_FAILED` | `test_returns_error_when_first_wasm_call_for_output_size_fails`, `test_returns_error_when_second_wasm_call_for_output_bytes_fails` |
| `ECALL_WASM_RESULT_RESOURCE_EXHAUSTED` | `test_returns_error_when_input_buffer_allocation_in_wasm_memory_fails`, `test_returns_error_when_output_buffer_allocation_in_wasm_memory_fails` |
| `ECALL_WASM_RESULT_OUTPUT_BUFFER_TOO_SMALL` | `test_returns_error_when_required_output_size_exceeds_host_output_buffer` |
| `ECALL_WASM_RESULT_OK` | `test_returns_success_and_copies_output_when_all_wasm_steps_succeed` |

### 8.2 What Unit Tests Confirmed
- Returns `ECALL_WASM_RESULT_INVALID_ARGUMENT` for invalid required arguments.
- Returns `ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND` when Trusted Component is missing/unavailable.
- Returns `ECALL_WASM_RESULT_RESOURCE_EXHAUSTED` when resources are insufficient.
- Returns `ECALL_WASM_RESULT_WASM_INCOMPATIBLE` when WASM binary is incompatible.
- Returns `ECALL_WASM_RESULT_WASM_EXECUTION_FAILED` when WASM execution fails.
- In success path, output is copied, `actual_len` is set, and `ECALL_WASM_RESULT_OK` is returned.

### 8.3 Items Not Covered Yet
- Reuse path in `ensure_wamr_running` (same `wapp_hash`)
- Reload path in `ensure_wamr_running` (different `wapp_hash`)
- `malloc` failure for copying `wapp_bin` (`ECALL_WASM_RESULT_RESOURCE_EXHAUSTED`)
- Branch where `actual_len == 0` after the second call

## 9. Future Work
- Design an input ABI (for example CBOR) for invoking WASM functions with multiple arguments.
