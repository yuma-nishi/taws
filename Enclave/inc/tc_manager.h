/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef TC_MANAGER_H
#define TC_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include "tc_list_types.h"

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "csuit/suit_manifest_process.h"
#include "csuit/suit_common.h"
#include "csuit/suit_digest.h"
#ifdef __cplusplus
}
#undef delete
#endif

typedef struct manifest_record {
    UsefulBuf   manifest_digest;
    char     manifest_name[SUIT_MAX_NAME_LENGTH];
    uint64_t      manifest_sequence_number;
    UsefulBuf manifest_bin;
    char     wapp_name[SUIT_MAX_NAME_LENGTH];
    uint8_t       wapp_hash[SHA256_DIGEST_LENGTH];
    UsefulBuf wapp_bin;
} manifest_record_t;

#define TC_MANAGER_RECORD_MAX_COUNT 20

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Store Trusted Component based on the suit_store_args_t into the manifest record table.
 *
 * \param[in] store_args SUIT store callback arguments.
 * \return 0 on success, -1 on error.
 */
int tc_manager_store_record_from_store_args(suit_store_args_t *store_args);

int tc_manager_store_record_from_fetch_args(); // TODO

/*!
 * \brief Build tc-list items from stored complete records.
 *
 * \param[out] out_items Caller-allocated output array.
 * \param[in] capacity Number of writable elements in out_items.
 * \param[out] out_count Number of items actually written.
 * \return 0 on success, -1 on error.
 */
int tc_manager_get_tc_list(tc_list_item_t *out_items, size_t capacity, size_t *out_count);

/*!
 * \brief Find a record by manifest digest.
 *
 * \param[in] manifest_digest Encoded SUIT manifest digest.
 *
 * \return Pointer to the matching record, or NULL if not found.
 */
const manifest_record_t *tc_manager_find_record_by_digest(UsefulBufC manifest_digest);

/*!
 * \brief Find a record by wapp name.
 *
 * \param[in] wapp_name Wasm app name.
 *
 * \return Pointer to the matching record, or NULL if not found.
 */
const manifest_record_t *tc_manager_find_record_by_wappname(const char *wapp_name);

/*!
 * \brief Check whether each record is complete, then apply the update or discard rules.
 *
 * Incomplete records are removed.
 *
 * \param[in] manifest_digest Manifest digest of the target record.
 * \return 0 on success, -1 on error.
 */
int tc_manager_check_and_update_record(UsefulBufC manifest_digest);

/*!
 * \brief Get the number of stored records.
 *
 * \return Number of active records.
 */
size_t tc_manager_record_count(void);

/*!
 * \brief Print all stored records for debugging.
 */
void tc_manager_dump_records(void);

/*!
 * \brief Remove and free all stored records.
 */
void tc_manager_remove_all(void);

#ifdef __cplusplus
}
#endif

#endif /* TC_MANAGER_H */
