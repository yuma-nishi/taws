/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "tc_manager.h"

#include "Enclave.h"
#include "debug_print.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>


#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "csuit/suit_manifest_print.h"
#ifdef __cplusplus
}
#undef delete
#endif

#ifdef __cplusplus
#define delete delete_perm
extern "C" {
#endif
#include "csuit/suit_manifest_decode.h"
#ifdef __cplusplus
}
#undef delete
#endif


static manifest_record_t g_manifest_records[TC_MANAGER_RECORD_MAX_COUNT] = {};
// Number of active entries in g_manifest_records.
static size_t g_manifest_record_len = 0;

static void manifest_record_remove_at(size_t index);
static void manifest_record_update_or_discard(manifest_record_t *record);
static manifest_record_t *manifest_records_alloc(void);
static manifest_record_t *manifest_record_find_by_digest(UsefulBufC digest);
static const manifest_record_t *manifest_record_find_by_wapp_name(const char *wapp_name);
static size_t manifest_record_count(void);
static const manifest_record_t *manifest_record_get(size_t index);
static void manifest_record_dump_all(void);
static bool manifest_record_is_complete(const manifest_record_t *record);




static bool manifest_record_set_ids_from_component_id(UsefulBufC encoded_component_id,
                                                      char *filename,
                                                      size_t filename_len)
{
    suit_component_identifier_t component_id;
    memset(&component_id, 0, sizeof(component_id));
    if (filename == NULL || filename_len == 0 || UsefulBuf_IsNULLOrEmptyC(encoded_component_id)) {
        return false;
    }
    if (suit_decode_component_identifier(encoded_component_id, &component_id) != SUIT_SUCCESS) {
        return false;
    }
    if (suit_component_identifier_to_filename(&component_id, filename_len, filename) != SUIT_SUCCESS) {
        return false;
    }
    const char *trimmed = filename;
    while (*trimmed == '/') {
        trimmed++;
    }
    if (trimmed != filename) {
        size_t trimmed_len = strnlen(trimmed, filename_len - 1);
        memmove(filename, trimmed, trimmed_len);
        filename[trimmed_len] = '\0';
    }
    return true;
}

extern "C" int tc_manager_store_record_from_store_args(suit_store_args_t *store_args)
{
    if (store_args == NULL) {
        return -1;
    }
    if (store_args->operation != SUIT_STORE) {
        return -1;
    }

    manifest_record_t *record = manifest_record_find_by_digest(store_args->manifest_digest);
    if (record == NULL) {
        record = manifest_records_alloc();
        if (record == NULL) {
            PRINT_DEBUG_LOG("[TC Manager] manifest record buffer full; skipping record\n");
            return -1;
        }
        // set manifest_digest (encoded SUIT_Digest)
        if (store_args->manifest_digest.ptr == NULL || store_args->manifest_digest.len == 0) {
            // if store_args->manifest_digest is null, delete an allocated record
            size_t record_index = (size_t)(record - g_manifest_records);
            if (record_index < g_manifest_record_len) {
                manifest_record_remove_at(record_index);
            }
            return -1;
        }
        record->manifest_digest.ptr = malloc(store_args->manifest_digest.len);
        if (record->manifest_digest.ptr == NULL) {
            size_t record_index = (size_t)(record - g_manifest_records);
            if (record_index < g_manifest_record_len) {
                manifest_record_remove_at(record_index);
            }
            return -1;
        }
        memcpy(record->manifest_digest.ptr, store_args->manifest_digest.ptr, store_args->manifest_digest.len);
        record->manifest_digest.len = store_args->manifest_digest.len;
    }

    if (store_args->is_manifest_itself) {
        // set manifest_sequence_number, manifest_name
        record->manifest_sequence_number = store_args->manifest_sequence_number;
        manifest_record_set_ids_from_component_id(store_args->dst,
                                                      record->manifest_name,
                                                      sizeof(record->manifest_name));

        // set manifest_bin
        if (record->manifest_bin.ptr != NULL) {
            free(record->manifest_bin.ptr);
            record->manifest_bin.ptr = NULL;
            record->manifest_bin.len = 0;
        }
        if (!UsefulBuf_IsNULLOrEmptyC(store_args->src_buf)) {
            record->manifest_bin.ptr = (uint8_t *)malloc(store_args->src_buf.len);
            if (record->manifest_bin.ptr != NULL) {
                memcpy(record->manifest_bin.ptr, store_args->src_buf.ptr, store_args->src_buf.len);
                record->manifest_bin.len = store_args->src_buf.len;
            } else {
                return -1;
            }
        }

    }else{
        // set wapp_name, wapp_hash, wapp_bin
        manifest_record_set_ids_from_component_id(store_args->dst,
                                                      record->wapp_name,
                                                      sizeof(record->wapp_name));

        if (record->wapp_bin.ptr != NULL) {
            free(record->wapp_bin.ptr);
            record->wapp_bin.ptr = NULL;
            record->wapp_bin.len = 0;
        }
        if (!UsefulBuf_IsNULLOrEmptyC(store_args->src_buf)) {
            record->wapp_bin.ptr = (uint8_t *)malloc(store_args->src_buf.len);
            if (record->wapp_bin.ptr != NULL) {
                memcpy(record->wapp_bin.ptr, store_args->src_buf.ptr, store_args->src_buf.len);
                record->wapp_bin.len = store_args->src_buf.len;
                (void)suit_generate_sha256(store_args->src_buf,
                                        UsefulBuf_FROM_BYTE_ARRAY(record->wapp_hash));
            } else {
                return -1;
            }
        }
    }
    return 0;
}

extern "C" const manifest_record_t *tc_manager_find_record_by_digest(UsefulBufC digest)
{
    return manifest_record_find_by_digest(digest);
}

extern "C" const manifest_record_t *tc_manager_find_record_by_wappname(const char *wapp_name)
{
    return manifest_record_find_by_wapp_name(wapp_name);
}

extern "C" int tc_manager_get_tc_list(tc_list_item_t *out_items, size_t capacity, size_t *out_count)
{
    if (out_count == NULL) {
        return -1;
    }
    *out_count = 0;

    if (capacity == 0 || out_items == NULL) {
        PRINT_DEBUG_LOG("[TC Manager] tc_list output buffer is invalid (capacity=%zu)\n", capacity);
        return -1;
    }

    size_t count = manifest_record_count();
    if (count == 0) {
        return 0;
    }
    if (capacity < count) {
        PRINT_DEBUG_LOG("[TC Manager] tc_list capacity too small (capacity=%zu required=%zu)\n",
                        capacity, count);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        const manifest_record_t *record = manifest_record_get(i);
        if (record == NULL || !manifest_record_is_complete(record)) {
            PRINT_DEBUG_LOG("[TC Manager] tc_list contains incomplete or invalid record at index=%zu\n", i);
            *out_count = 0;
            return -1;
        }
        size_t name_len = strnlen(record->wapp_name, SUIT_MAX_NAME_LENGTH);
        if (name_len == 0 || name_len >= SUIT_MAX_NAME_LENGTH ||
            name_len > TC_COMPONENT_ID_MAX_LEN) {
            PRINT_DEBUG_LOG("[TC Manager] invalid wapp_name at index=%zu\n", i);
            *out_count = 0;
            return -1;
        }
        memcpy(out_items[i].component_id, record->wapp_name, name_len);
        out_items[i].component_id[name_len] = '\0';
        memcpy(out_items[i].tc_image_digest, record->wapp_hash, SHA256_DIGEST_LENGTH);
    }
    *out_count = count;
    return 0;
}


extern "C" int tc_manager_check_and_update_record(UsefulBufC digest)
{
    manifest_record_t *record = manifest_record_find_by_digest(digest);
    if (record == NULL) {
        return -1;
    }
    if (!manifest_record_is_complete(record)) {
        PRINT_DEBUG_LOG("[TC Manager] manifest record incomplete; discarding\n");
        size_t record_index = (size_t)(record - g_manifest_records);
        if (record_index < g_manifest_record_len) {
            manifest_record_remove_at(record_index);
        }
        return -1;
    }
    manifest_record_update_or_discard(record);
    return 0;
}

extern "C" size_t tc_manager_record_count(void)
{
    return manifest_record_count();
}

extern "C" void tc_manager_dump_records(void)
{
    manifest_record_dump_all();
}

extern "C" void tc_manager_remove_all(void)
{
    while (g_manifest_record_len > 0) {
        manifest_record_remove_at(0);
    }
}


/*!
    \brief      Remove a manifest record at the given index.

    \param[in]  index   Index into the manifest record array to remove.
*/
static void manifest_record_remove_at(size_t index)
{
    if (index >= g_manifest_record_len) {
        return;
    }
    size_t last = g_manifest_record_len - 1;
    if (index != last) {
        manifest_record_t tmp = g_manifest_records[index];
        g_manifest_records[index] = g_manifest_records[last];
        g_manifest_records[last] = tmp;
    }
    if (g_manifest_records[last].manifest_digest.ptr != NULL) {
        free(g_manifest_records[last].manifest_digest.ptr);
    }
    if (g_manifest_records[last].manifest_bin.ptr != NULL) {
        free(g_manifest_records[last].manifest_bin.ptr);
    }
    if (g_manifest_records[last].wapp_bin.ptr != NULL) {
        free(g_manifest_records[last].wapp_bin.ptr);
    }
    memset(&g_manifest_records[last], 0, sizeof(g_manifest_records[last]));
    g_manifest_record_len--;
}

/*!
    \brief      Update or discard a record when a duplicate wapp_name exists.

    \param[in]  record  Newly added record to compare against existing entries.
*/
static void manifest_record_update_or_discard(manifest_record_t *record)
{
    if (record == NULL || record->wapp_name[0] == '\0') {
        return;
    }
    // Pointer difference gives the element index within the manifest record array.
    size_t record_index = (size_t)(record - g_manifest_records);
    if (record_index >= g_manifest_record_len) {
        return;
    }

    for (size_t i = 0; i < g_manifest_record_len; i++) {
        if (i == record_index) {
            continue;
        }
        manifest_record_t *other = &g_manifest_records[i];
        if (other->wapp_name[0] == '\0') {
            continue;
        }
        if (strcmp(other->wapp_name, record->wapp_name) != 0) {
            continue;
        }

        if (other->manifest_sequence_number <= record->manifest_sequence_number) {
            PRINT_DEBUG_LOG("[TC Manager] manifest update: wapp=%s old_version=%" PRIu64 " new_version=%" PRIu64 "\n",
                            other->wapp_name,
                            other->manifest_sequence_number,
                            record->manifest_sequence_number);
            manifest_record_t tmp = *other;
            *other = g_manifest_records[record_index];
            g_manifest_records[record_index] = tmp;
            manifest_record_remove_at(record_index);
        } else {
            PRINT_DEBUG_LOG("[TC Manager] manifest outdated: wapp=%s existing_version=%" PRIu64 " new_version=%" PRIu64 "; discarding new\n",
                            record->wapp_name,
                            other->manifest_sequence_number,
                            record->manifest_sequence_number);
            manifest_record_remove_at(record_index);
        }
        return;
    }
}

static bool manifest_record_is_complete(const manifest_record_t *record)
{
    if (record == NULL) {
        return false;
    }
    if (record->manifest_name[0] == '\0' || record->wapp_name[0] == '\0') {
        return false;
    }
    if (UsefulBuf_IsNULLOrEmpty(record->manifest_bin)) {
        return false;
    }
    if (UsefulBuf_IsNULLOrEmpty(record->wapp_bin)) {
        return false;
    }
    return true;
}

/*!
    \brief      Allocate a new manifest record slot.

    \return     Pointer to the allocated record, or NULL if full.
*/
static manifest_record_t *manifest_records_alloc(void)
{
    if (g_manifest_record_len >= TC_MANAGER_RECORD_MAX_COUNT) {
        return NULL;
    }
    size_t index = g_manifest_record_len;
    g_manifest_record_len++;
    return &g_manifest_records[index];
}

/*!
    \brief      Find a manifest record by SHA-256 digest bytes.

    \param[in]  digest  Digest bytes to match (expects SHA256_DIGEST_LENGTH).
    \return     Pointer to record, or NULL if not found.
*/
static manifest_record_t *manifest_record_find_by_digest(UsefulBufC digest)
{
    if (digest.ptr == NULL || digest.len == 0) {
        return NULL;
    }
    for (size_t i = 0; i < g_manifest_record_len; i++) {
        manifest_record_t *record = &g_manifest_records[i];
        if (record->manifest_digest.ptr == NULL ||
            record->manifest_digest.len != digest.len) {
            continue;
        }
        if (memcmp(record->manifest_digest.ptr, digest.ptr, digest.len) == 0) {
            return record;
        }
    }
    return NULL;
}

static const manifest_record_t *manifest_record_find_by_wapp_name(const char *wapp_name)
{
    if (wapp_name == NULL || wapp_name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < g_manifest_record_len; i++) {
        const manifest_record_t *record = &g_manifest_records[i];
        if (record->wapp_name[0] == '\0') {
            continue;
        }
        if (strcmp(record->wapp_name, wapp_name) == 0) {
            return record;
        }
    }
    return NULL;
}

/*!
    \brief      Set the currently active manifest record.

    \param[in]  record  Record to mark as active (may be NULL).
*/
/*!
    \brief      Get the number of active manifest records.

    \return     Number of records currently stored.
*/
static size_t manifest_record_count(void)
{
    return g_manifest_record_len;
}

/*!
    \brief      Get a read-only record by index.

    \param[in]  index   Record index to access.
    \return     Pointer to the record, or NULL if out of range.
*/
static const manifest_record_t *manifest_record_get(size_t index)
{
    if (index >= g_manifest_record_len) {
        return NULL;
    }
    return &g_manifest_records[index];
}

/*!
    \brief      Print all manifest records to the debug output.
*/
static void manifest_record_dump_all(void)
{
    PRINT_DEBUG_LOG("[TC Manager] manifest records: %zu\n", g_manifest_record_len);
    for (size_t i = 0; i < g_manifest_record_len; i++) {
        const manifest_record_t *record = manifest_record_get(i);
        if (record == NULL) {
            continue;
        }
        PRINT_DEBUG_LOG("  [%zu] name=%s wapp=%s seq=%" PRIu64 " len=%zu\n",
                        i,
                        record->manifest_name,
                        record->wapp_name,
                        record->manifest_sequence_number,
                        record->manifest_bin.len);
    }
}
