/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TEEP_SESSION_RESULT_H
#define TEEP_SESSION_RESULT_H

typedef enum {
    TEEP_SESSION_RESULT_OK = 0,
    TEEP_SESSION_RESULT_TEEP_ERROR_RESPONSE = 1,
    TEEP_SESSION_RESULT_FATAL = 2,
    TEEP_SESSION_RESULT_HTTP_ERROR = 3,
    TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED = 4
} teep_session_result_t;

#endif /* TEEP_SESSION_RESULT_H */
