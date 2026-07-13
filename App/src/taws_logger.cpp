/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "taws_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static taws_log_level_t g_taws_log_level = TAWS_LOG_LEVEL_INFO;

#define TAWS_COLOR_CYAN                 "\033[36m"
#define TAWS_COLOR_GREEN                "\033[32m"
#define TAWS_COLOR_RESET                "\033[0m"

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

static int stdout_supports_color(void)
{
    return isatty(STDOUT_FILENO);
}

static int is_teep_receive_log(const char *str)
{
    return strstr(str, "received TEEP QueryRequest") != NULL ||
           strstr(str, "received TEEP Update") != NULL;
}

static int is_teep_install_finished_log(taws_log_level_t level, const char *fmt)
{
    return level == TAWS_LOG_LEVEL_INFO &&
           (strcmp(fmt, "TEEP install session finished: device activation flow") == 0 ||
            strcmp(fmt, "TEEP install session finished") == 0);
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

    FILE *stream = (level == TAWS_LOG_LEVEL_ERROR) ? stderr : stdout;
    int use_color = is_teep_install_finished_log(level, fmt) && stdout_supports_color();
    if (use_color) {
        fputs(TAWS_COLOR_GREEN, stream);
    }
    fprintf(stream, "[TEEP Broker] [%s] ", level_name(level));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stream, fmt, ap);
    va_end(ap);

    if (use_color) {
        fputs(TAWS_COLOR_RESET, stream);
    }
    fputc('\n', stream);
    fflush(stream);
}

void taws_log_teep_send(const char *fmt, ...)
{
    if (!taws_log_is_enabled(TAWS_LOG_LEVEL_INFO)) {
        return;
    }

    FILE *stream = stdout;
    int use_color = stdout_supports_color();
    if (use_color) {
        fputs(TAWS_COLOR_CYAN, stream);
    }
    fprintf(stream, "[TEEP Broker] [%s] ", level_name(TAWS_LOG_LEVEL_INFO));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stream, fmt, ap);
    va_end(ap);

    if (use_color) {
        fputs(TAWS_COLOR_RESET, stream);
    }
    fputc('\n', stream);
    fflush(stream);
}

void taws_log_print_enclave_ocall(const char *str)
{
    if (stdout_supports_color() && is_teep_receive_log(str)) {
        fputs(TAWS_COLOR_GREEN, stdout);
        fputs(str, stdout);
        fputs(TAWS_COLOR_RESET, stdout);
    } else {
        fputs(str, stdout);
    }
    fflush(stdout);
}
