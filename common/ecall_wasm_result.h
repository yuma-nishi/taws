/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ECALL_WASM_RESULT_H
#define ECALL_WASM_RESULT_H

/*! @brief Result returned by `ecall_invoke_wasm` for caller-side success/failure handling. */
typedef enum {
    ECALL_WASM_RESULT_OK = 0,                           /*! Success. */
    ECALL_WASM_RESULT_INVALID_ARGUMENT = -1,            /*! Invalid argument. */
    ECALL_WASM_RESULT_TRUSTED_COMPONENT_NOT_FOUND = -2, /*! Target Trusted Component not found. */
    ECALL_WASM_RESULT_RESOURCE_EXHAUSTED = -3,          /*! Resource exhausted. */
    ECALL_WASM_RESULT_WASM_INCOMPATIBLE = -4,           /*! WASM binary/function is incompatible. */
    ECALL_WASM_RESULT_WASM_EXECUTION_FAILED = -5,       /*! WASM execution failed. */
    ECALL_WASM_RESULT_INTERNAL_ERROR = -6,              /*! Internal error. */
    ECALL_WASM_RESULT_OUTPUT_BUFFER_TOO_SMALL = -7      /*! Output buffer too small. */
} ecall_wasm_result_t;

#endif /* ECALL_WASM_RESULT_H */
