/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TEEP_HTTP_CLIENT_H
#define TEEP_HTTP_CLIENT_H

#include <curl/curl.h>
#include "qcbor/UsefulBuf.h"

// out_recv_buffer: input len = capacity, output len = received size.
int teep_send_http_post(const char *url, UsefulBufC send_buffer, UsefulBuf *out_recv_buffer);

#endif  // TEEP_HTTP_CLIENT_H
