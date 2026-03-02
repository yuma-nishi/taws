/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _ENCLAVE_H_
#define _ENCLAVE_H_

#include <stdlib.h>
#include <assert.h>
#include "ecall_wasm_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

int printf(const char *fmt, ...);

typedef enum {
    TEEP_KEY_UNINITIALIZED = 0,
    TEEP_KEY_READY
} teep_key_state_t;

extern teep_key_state_t g_key_state;

#if defined(__cplusplus)
}
#endif

#endif /* !_ENCLAVE_H_ */
