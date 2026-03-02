/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ATTESTER_API_H
#define ATTESTER_API_H

#include <stddef.h>
#include <stdint.h>
#include "teep_session_result.h"

#ifdef __cplusplus
extern "C" {
#endif

int attester_init(const char *keygen_mode);
teep_session_result_t attester_install(const char *tam_url, const char *app_name);
int attester_invoke_wasm(const char *wapp_name,
                               const char *func_name,
                               const uint8_t *input,
                               size_t input_len,
                               uint8_t *output,
                               size_t output_len,
                               size_t *actual_len);
void attester_close(void);

#ifdef __cplusplus
}
#endif

#endif  /* ATTESTER_API_H */
