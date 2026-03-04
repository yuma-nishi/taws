/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ECALL_PROCESS_TEEP_RESULT_H
#define ECALL_PROCESS_TEEP_RESULT_H

/*! @brief Result returned by `ecall_process_message` for caller-side session handling. */
typedef enum {
    ECALL_PROCESS_TEEP_RESULT_OK = 0,                        /*! Success. */
    ECALL_PROCESS_TEEP_RESULT_RESPONSE_IS_TEEP_ERROR = 1,    /*! Success, but response type is TEEP Error. */
    ECALL_PROCESS_TEEP_RESULT_FATAL = 2,                     /*! Fatal failure (for example verify/decode/encode/sign). */
    ECALL_PROCESS_TEEP_RESULT_DEVICE_ACTIVATION_FLOW = 3     /*! Success with device activation flow (QUERY_RESPONSE with attestation payload). */
} ecall_process_teep_result_t;

#endif /* ECALL_PROCESS_TEEP_RESULT_H */
