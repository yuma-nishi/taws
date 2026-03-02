/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef DEBUG
    #define PRINT_DEBUG_LOG(...) \
        do { printf(__VA_ARGS__); } while (0)
    #define TEEP_DEBUG_QUERY(req, indent, level) \
        do{\
            printf("[DEBUG] TEEP_DEBUG_QUERY called\n"); \
            teep_print_query_request((const teep_query_request_t *)(req), (indent), (level));\
        }while(0)
    #define TEEP_DEBUG_UPDATE(upd, indent, level, key) \
        do{\
            printf("[DEBUG] TEEP_DEBUG_UPDATE called\n"); \
            teep_print_update((const teep_update_t *)(upd), (indent), (level), (key)); \
        }while(0)
    #define TEEP_DEBUG_QUERY_RESPONSE(resp, indent, level) \
        do{\
            printf("[DEBUG] TEEP_DEBUG_QUERY_RESPONSE called\n"); \
            teep_print_query_response((const teep_query_response_t*)(resp), (indent), (level)); \
        }while(0)
#else
    #define PRINT_DEBUG_LOG(...) \
        do { } while (0)
    #define TEEP_DEBUG_QUERY(req, indent, level) \
        do { (void)(req); (void)(indent); (void)(level); } while (0)
    #define TEEP_DEBUG_UPDATE(upd, indent, level, key) \
        do { (void)(upd); (void)(indent); (void)(level); (void)(key); } while (0)
    #define TEEP_DEBUG_QUERY_RESPONSE(resp, indent, level) \
        do { (void)(resp); (void)(indent); (void)(level); } while (0)

#endif
