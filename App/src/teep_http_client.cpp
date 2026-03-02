/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "teep/teep_cose.h"
#include "teep_http_client.h"

#include <string.h>

typedef struct {
    UsefulBuf out;
    const size_t allocated_len;
} teep_http_recv_context_t;

static size_t write_callback(void *recv_buffer_ptr,
                             size_t size,
                             size_t nitems,
                             void *user_ptr)
{

    teep_http_recv_context_t *ctx = (teep_http_recv_context_t *)user_ptr;
    size_t recv_size = size * nitems;
    size_t remain_size = ( ctx->out.len < ctx->allocated_len) ? (ctx->allocated_len - ctx->out.len) : 0;
    size_t copy_size = (recv_size < remain_size) ? recv_size : remain_size;

    if(copy_size > 0){
        memcpy((unsigned char *)ctx->out.ptr + ctx->out.len, recv_buffer_ptr, copy_size);
        ctx->out.len += copy_size;
    }
    else{
        printf("write_callback : buffer overflow. remain_size =%ld < recv_size =%ld\n", remain_size, recv_size);
    }

    return recv_size;


}

int teep_send_http_post(const char *url,
                               UsefulBufC send_buffer,
                               UsefulBuf *out_recv_buffer)
{
    int          result = 0;
    CURL                *curl = NULL;
    CURLcode            curl_result;
    struct curl_slist   *curl_slist = NULL;
    teep_http_recv_context_t recv_context = {
        .out = (UsefulBuf){
            .ptr = out_recv_buffer->ptr,
            .len = 0
        },
        .allocated_len = out_recv_buffer->len
    };

    out_recv_buffer->len = 0;

    // Set parameter.
    printf("[TEEP Broker] > HTTP POST %s\n", url);
    curl = curl_easy_init();
    if (curl == NULL) {
        printf("teep_send_post_request : curl_easy_init : Fail.\n");
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_slist = curl_slist_append(curl_slist, "Accept: application/teep+cbor");
    curl_slist = curl_slist_append(curl_slist, "User-Agent: Foo/1.0");
    curl_slist = curl_slist_append(curl_slist, "Content-Type: application/teep+cbor");
    if (UsefulBuf_IsNULLOrEmptyC(send_buffer)) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    }
    else {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, send_buffer.len);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send_buffer.ptr);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_slist);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &recv_context);

    if (recv_context.out.len > 0 && recv_context.out.len <= LONG_MAX) {
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)recv_context.out.len);
    }
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    // Send request.
    do{
        curl_result = curl_easy_perform(curl);
        if (curl_result != CURLE_OK) {
            printf("teep_send_post_request : curl_easy_perform : Fail. (%s)\n",
                   curl_easy_strerror(curl_result));
            result = 1;
            break;
        }

        // Get status code.
        int64_t response_code = -1;
        curl_result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (curl_result == CURLE_OK && response_code == 204) {
            result = 0;
            break;
        }
        if (curl_result != CURLE_OK || response_code < 0 || response_code != 200) {
            printf("HTTP status: %ld\n", (long)response_code);
            result = 2;
            break;
        }
    }while(0);

    out_recv_buffer->len = recv_context.out.len;

    curl_easy_cleanup(curl);
    return result;
}
