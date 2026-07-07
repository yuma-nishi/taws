/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "taws_logger.h"

#ifdef DEBUG
#define PRINT_DEBUG_LOG(...) TAWS_LOG_DEBUG(__VA_ARGS__)
#else
#define PRINT_DEBUG_LOG(...) do { } while (0)
#endif
#define TEEP_DEBUG_QUERY(req, indent, level) TAWS_LOG_DEBUG_TEEP_QUERY((req), (indent), (level))
#define TEEP_DEBUG_UPDATE(upd, indent, level, key) TAWS_LOG_DEBUG_TEEP_UPDATE((upd), (indent), (level), (key))
#define TEEP_DEBUG_QUERY_RESPONSE(resp, indent, level) TAWS_LOG_DEBUG_TEEP_QUERY_RESPONSE((resp), (indent), (level))
