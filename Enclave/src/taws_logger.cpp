/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "taws_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Enclave_t.h"

static taws_log_level_t g_taws_log_level = TAWS_LOG_LEVEL_INFO;

static const char *level_name(taws_log_level_t level)
{
    switch (level) {
    case TAWS_LOG_LEVEL_ERROR:
        return "ERROR";
    case TAWS_LOG_LEVEL_INFO:
        return "INFO";
    case TAWS_LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "INFO";
    }
}

extern "C" void ecall_set_log_level(int level)
{
    taws_log_set_level((taws_log_level_t)level);
}

void taws_log_set_level(taws_log_level_t level)
{
    if (level < TAWS_LOG_LEVEL_ERROR || level > TAWS_LOG_LEVEL_DEBUG) {
        return;
    }
    g_taws_log_level = level;
}

int taws_log_is_enabled(taws_log_level_t level)
{
    return level <= g_taws_log_level;
}

void taws_log(taws_log_level_t level, const char *fmt, ...)
{
    if (!taws_log_is_enabled(level)) {
        return;
    }

    char buf[BUFSIZ] = {'\0'};
    int prefix_len = snprintf(buf, sizeof(buf), "[TEEP Agent] [%s] ", level_name(level));
    if (prefix_len < 0) {
        return;
    }
    if ((size_t)prefix_len >= sizeof(buf)) {
        buf[sizeof(buf) - 1] = '\0';
        ocall_print_string(buf);
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int msg_len = vsnprintf(buf + prefix_len, sizeof(buf) - (size_t)prefix_len, fmt, ap);
    va_end(ap);
    if (msg_len < 0) {
        return;
    }

    size_t len = strnlen(buf, sizeof(buf));
    if (len + 1 < sizeof(buf)) {
        buf[len] = '\n';
        buf[len + 1] = '\0';
    }
    ocall_print_string(buf);
}
