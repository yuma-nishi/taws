/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// create_evidence.h
#ifndef CREATE_EVIDENCE_H
#define CREATE_EVIDENCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <teep/teep_message_print.h>
#include "teep/teep_cose.h"
#include "teep/teep_common.h"
#include "teep/teep_message_data.h"

#ifdef __cplusplus
}
#endif

/*!
    \brief      Create evidence with RATS EAT.

    \param[in]      query_request   Received teep-query-request message from the TAM.
    \param[in]      buf          Allocated buffer.
    \param[in]      teep_agent_key_pair The Evidence includes the TEEP Agent’s public key.
    \param[out]     ret             Pointer of the output struct.

    \return     This returns TEEP_AGENT_SUCCESS or TEEP_AGENT_ERR_FAILED_TO_CREATE_EVIDENCE.
*/
teep_err_t create_evidence_generic(const teep_query_request_t *query_request,
                           UsefulBuf buf,
                           teep_key_t *key_pair,
                           UsefulBufC *ret);

teep_err_t create_evidence_dcap(const teep_query_request_t *query_request,
                           UsefulBuf buf,
                           teep_key_t *key_pair,
                           UsefulBufC *ret);

teep_err_t create_evidence_dcap_envelope(const teep_query_request_t *query_request,
                           UsefulBuf buf,
                           teep_key_t *key_pair,
                           UsefulBufC *ret);


#endif /* CREATE_EVIDENCE_H */
