/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TAWS_LOGGER_H
#define TAWS_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG
#include "teep/teep_message_print.h"
#endif

typedef enum taws_log_level_t {
    TAWS_LOG_LEVEL_ERROR = 0,
    TAWS_LOG_LEVEL_INFO = 1,
    TAWS_LOG_LEVEL_DEBUG = 2,
} taws_log_level_t;

void taws_log_set_level(taws_log_level_t level);
int taws_log_is_enabled(taws_log_level_t level);
void taws_log(taws_log_level_t level, const char *fmt, ...);
void taws_log_teep_send(const char *fmt, ...);
void taws_log_print_enclave_ocall(const char *str);

#ifdef DEBUG
teep_err_t taws_teep_dump_message(const teep_message_t *message,
                                  uint32_t indent_space,
                                  uint32_t indent_delta,
                                  const unsigned char *ta_public_key);
#endif

#ifdef __cplusplus
}
#endif

#define TAWS_LOG_ERROR(...) \
    do { taws_log(TAWS_LOG_LEVEL_ERROR, __VA_ARGS__); } while (0)
#define TAWS_LOG_INFO(...) \
    do { taws_log(TAWS_LOG_LEVEL_INFO, __VA_ARGS__); } while (0)
#define TAWS_LOG_DEBUG(...) \
    do { taws_log(TAWS_LOG_LEVEL_DEBUG, __VA_ARGS__); } while (0)

#ifdef DEBUG
#define TAWS_LOG_DEBUG_TEEP_DUMP(field, value, indent, level, key, title) \
    do { \
        if (taws_log_is_enabled(TAWS_LOG_LEVEL_DEBUG)) { \
            teep_message_t taws_teep_dump_msg; \
            taws_teep_dump_msg.field = *(value); \
            TAWS_LOG_DEBUG(title); \
            taws_teep_dump_message(&taws_teep_dump_msg, (indent), (level), (key)); \
        } \
    } while (0)

#define TAWS_LOG_DEBUG_TEEP_QUERY(req, indent, level) \
    TAWS_LOG_DEBUG_TEEP_DUMP(query_request, (const teep_query_request_t *)(req), \
                             (indent), (level), NULL, "TEEP QueryRequest dump")
#define TAWS_LOG_DEBUG_TEEP_UPDATE(upd, indent, level, key) \
    TAWS_LOG_DEBUG_TEEP_DUMP(teep_update, (const teep_update_t *)(upd), \
                             (indent), (level), (key), "TEEP Update dump")
#define TAWS_LOG_DEBUG_TEEP_QUERY_RESPONSE(resp, indent, level) \
    TAWS_LOG_DEBUG_TEEP_DUMP(query_response, (const teep_query_response_t *)(resp), \
                             (indent), (level), NULL, "TEEP QueryResponse dump")
#else
#define TAWS_LOG_DEBUG_TEEP_QUERY(req, indent, level) \
    do { (void)(req); (void)(indent); (void)(level); } while (0)
#define TAWS_LOG_DEBUG_TEEP_UPDATE(upd, indent, level, key) \
    do { (void)(upd); (void)(indent); (void)(level); (void)(key); } while (0)
#define TAWS_LOG_DEBUG_TEEP_QUERY_RESPONSE(resp, indent, level) \
    do { (void)(resp); (void)(indent); (void)(level); } while (0)
#endif

#endif /* TAWS_LOGGER_H */
