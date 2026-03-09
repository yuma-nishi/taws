/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*!
    \file   suit_processor_wrapper.cpp

    \brief  Wrapper callbacks that bridge libcsuit processing and Attester modules
 */

//#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif

#include "csuit/suit_manifest_process.h"
#include "csuit/suit_manifest_print.h"
#include "csuit/suit_digest.h"
#include "Enclave.h"
#include "suit_config.h"

#ifdef __cplusplus
}
#undef delete
#endif

#include "tc_manager.h"
#include "debug_print.h"


static UsefulBuf g_suit_report = NULLUsefulBuf;

static void suit_log_hex_prefix(const char *label,
                                const uint8_t *buf,
                                size_t len,
                                size_t max_bytes) __attribute__((unused));
static void suit_log_hex_prefix(const char *label,
                                const uint8_t *buf,
                                size_t len,
                                size_t max_bytes)
{
    size_t i = 0;
    size_t cap = (len < max_bytes) ? len : max_bytes;
    PRINT_DEBUG_LOG("%s (len=%zu): ", label, len);
    for (i = 0; i < cap; i++) {
        PRINT_DEBUG_LOG("%02X", buf[i]);
    }
    if (len > cap) {
        PRINT_DEBUG_LOG("...");
    }
    PRINT_DEBUG_LOG("\n");
}


static void suit_clear_report_internal(void)
{
    //Clear the stored SUIT report buffer.
    if (g_suit_report.ptr != NULL) {
        free(g_suit_report.ptr);
    }
    g_suit_report = NULLUsefulBuf;
}


extern "C" const uint8_t *suit_get_suit_report(size_t *len)
{
    if (len != NULL) {
        *len = g_suit_report.len;
    }
    return static_cast<const uint8_t *>(g_suit_report.ptr);
}

extern "C" suit_err_t __wrap_suit_store_callback(suit_store_args_t store_args)
{
    if (store_args.operation == SUIT_STORE) {
        PRINT_DEBUG_LOG("[SUIT] store: src_len=%zu\n", store_args.src_buf.len);
        int rc = tc_manager_store_record_from_store_args(&store_args);
        if (rc != 0) {
            return SUIT_ERR_FATAL;
        }
    }

    return SUIT_SUCCESS;
}

extern "C" suit_err_t __wrap_suit_invoke_callback(suit_invoke_args_t invoke_args)
{
    (void)invoke_args;
    return SUIT_SUCCESS;
}

extern "C" suit_err_t __wrap_suit_condition_callback(suit_condition_args_t condition_args,
                                                     suit_callback_ret_t *condition_ret)
{
    suit_err_t result = SUIT_SUCCESS;
    if (condition_ret == NULL) {
        return SUIT_ERR_INVALID_VALUE;
    }

    const manifest_record_t *record = tc_manager_find_record_by_digest(condition_args.manifest_digest);
    if (record == NULL) {
        condition_ret->reason = SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED;
        PRINT_DEBUG_LOG("[SUIT] condition: no manifest record found for digest (len=%zu)\n",
                        condition_args.manifest_digest.len);
        return SUIT_ERR_FATAL;
    }

    if (condition_args.condition == SUIT_CONDITION_IMAGE_MATCH) {

        size_t wapp_bin_size = (record != NULL) ? record->wapp_bin.len : 0;
        PRINT_DEBUG_LOG("[SUIT] condition-image-match: wapp_bin_size=%zu expected_size=%" PRIu64 "\n",
                        wapp_bin_size, condition_args.expected.u64);
        if (wapp_bin_size == 0) {
            condition_ret->reason = SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED;
            return SUIT_ERR_CONDITION_MISMATCH;
        }

        if (condition_args.expected.u64 != 0 &&
            wapp_bin_size != condition_args.expected.u64) {
            condition_ret->consumed_parameter_keys[0] = SUIT_PARAMETER_IMAGE_SIZE;
            condition_ret->reason = SUIT_REPORT_REASON_CONDITION_FAILED;
            return SUIT_ERR_CONDITION_MISMATCH;
        }

        // CBOR (digest-bytes + algorithm id） --> suit_digest_t
        suit_digest_t digest;
        result = suit_decode_digest(condition_args.expected.str, &digest);
        if (result != SUIT_SUCCESS) {
            condition_ret->reason = SUIT_REPORT_REASON_CBOR_PARSE;
            return result;
        }
        if (digest.algorithm_id != SUIT_ALGORITHM_ID_SHA256 ||
            digest.bytes.len != SHA256_DIGEST_LENGTH) {
            condition_ret->reason = SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED;
            return SUIT_ERR_NOT_IMPLEMENTED;
        }

        condition_ret->consumed_parameter_keys[0] = SUIT_PARAMETER_IMAGE_SIZE;
        condition_ret->consumed_parameter_keys[1] = SUIT_PARAMETER_IMAGE_DIGEST;

        bool match = (record != NULL &&
                      memcmp(digest.bytes.ptr,
                             record->wapp_hash,
                             SHA256_DIGEST_LENGTH) == 0);

        if (!match) {
            condition_ret->reason = SUIT_REPORT_REASON_CONDITION_FAILED;
            return SUIT_ERR_CONDITION_MISMATCH;
        }
        return SUIT_SUCCESS;
    }

    return result;
}


extern "C" suit_err_t __real_suit_report_callback(suit_report_args_t report_args);
extern "C" suit_err_t __wrap_suit_report_callback(suit_report_args_t report_args)
{
    suit_err_t result = SUIT_SUCCESS;
    const bool has_report = !UsefulBuf_IsNULLOrEmptyC(report_args.suit_report);
#ifdef DEBUG
    /*
     * libcsuit's default print callback expects a valid report buffer.
     * Guard empty reports to avoid NULL dereference in debug builds.
     */
    if (has_report) {
        result = __real_suit_report_callback(report_args);
        if (result != SUIT_SUCCESS) {
            return SUIT_ERR_WHILE_REPORTING;
        }
    }
#endif

    if (has_report) {
        uint8_t *copied_report = NULL;
        suit_clear_report_internal();
        copied_report = (uint8_t *)malloc(report_args.suit_report.len);
        if (copied_report == NULL) {
            return SUIT_ERR_NO_MEMORY;
        }
        memcpy(copied_report, report_args.suit_report.ptr, report_args.suit_report.len);
        g_suit_report = (UsefulBuf){ .ptr = copied_report, .len = report_args.suit_report.len };
    }

    return result;
}
