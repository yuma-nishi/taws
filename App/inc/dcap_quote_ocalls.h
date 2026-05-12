/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef DCAP_QUOTE_OCALLS_H
#define DCAP_QUOTE_OCALLS_H

#include <stdint.h>

#include "sgx_error.h"
#include "sgx_report.h"

#ifdef __cplusplus
extern "C" {
#endif

sgx_status_t ocall_get_qe_target_info(sgx_target_info_t *qe_target_info);
sgx_status_t ocall_get_quote_size(uint32_t *quote_size);
sgx_status_t ocall_get_quote(const sgx_report_t *report,
                             uint8_t *quote_buf,
                             uint32_t quote_size);

#ifdef __cplusplus
}
#endif

#endif /* DCAP_QUOTE_OCALLS_H */
