/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TC_LIST_TYPES_H
#define TC_LIST_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "csuit/suit_common.h"
#include "csuit/suit_digest.h"
#ifdef __cplusplus
}
#undef delete
#endif

#define TC_COMPONENT_ID_MAX_LEN (SUIT_MAX_NAME_LENGTH - 1)

typedef struct tc_list_item {
    char component_id[TC_COMPONENT_ID_MAX_LEN + 1];
    uint8_t tc_image_digest[SHA256_DIGEST_LENGTH];
} tc_list_item_t;

#endif /* TC_LIST_TYPES_H */
