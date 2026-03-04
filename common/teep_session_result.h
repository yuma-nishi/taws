/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TEEP_SESSION_RESULT_H
#define TEEP_SESSION_RESULT_H

/*! @brief Session-level result returned by the App layer after completing TEEP over HTTP flow. */
typedef enum {
    TEEP_SESSION_RESULT_OK = 0,                    /*! Success. */
    TEEP_SESSION_RESULT_TEEP_ERROR_RESPONSE = 1,   /*! Success, but response type is TEEP Error. */
    TEEP_SESSION_RESULT_FATAL = 2,                 /*! Fatal failure in session processing. */
    TEEP_SESSION_RESULT_HTTP_ERROR = 3,            /*! HTTP transport/status error. */
    TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED = 4    /*! Success with device activation flow. */
} teep_session_result_t;

#endif /* TEEP_SESSION_RESULT_H */
