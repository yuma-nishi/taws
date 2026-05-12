/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "dcap_quote_ocalls.h"

#include <stdio.h>

#include "sgx_dcap_ql_wrapper.h"

extern "C" sgx_status_t ocall_get_qe_target_info(sgx_target_info_t *qe_target_info)
{
    if (qe_target_info == NULL) {
        return SGX_ERROR_INVALID_PARAMETER;
    }

    quote3_error_t qret = sgx_qe_get_target_info(qe_target_info);
    if (qret != SGX_QL_SUCCESS) {
        fprintf(stderr, "sgx_qe_get_target_info failed: 0x%04x\n", qret);
        return SGX_ERROR_UNEXPECTED;
    }

    return SGX_SUCCESS;
}

extern "C" sgx_status_t ocall_get_quote_size(uint32_t *quote_size)
{
    if (quote_size == NULL) {
        return SGX_ERROR_INVALID_PARAMETER;
    }

    quote3_error_t qret = sgx_qe_get_quote_size(quote_size);
    if (qret != SGX_QL_SUCCESS) {
        fprintf(stderr, "sgx_qe_get_quote_size failed: 0x%04x\n", qret);
        return SGX_ERROR_UNEXPECTED;
    }

    return SGX_SUCCESS;
}

extern "C" sgx_status_t ocall_get_quote(const sgx_report_t *report,
                                         uint8_t *quote_buf,
                                         uint32_t quote_size)
{
    if (report == NULL || quote_buf == NULL || quote_size == 0) {
        return SGX_ERROR_INVALID_PARAMETER;
    }

    quote3_error_t qret = sgx_qe_get_quote(report, quote_size, quote_buf);
    if (qret != SGX_QL_SUCCESS) {
        fprintf(stderr, "sgx_qe_get_quote failed: 0x%04x\n", qret);
        return SGX_ERROR_UNEXPECTED;
    }

    return SGX_SUCCESS;
}
