/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "taws_logger.h"

extern "C" {
#include "teep/teep_message_print.h"
}

extern "C" int printf(const char *fmt, ...);

static teep_err_t taws_print_component_id(const teep_buf_t *component_id);
static const size_t TAWS_MAX_PRINT_TEXT_COUNT = 40;

static bool taws_is_printable_char(uint8_t c)
{
    return (' ' <= c && c <= '~');
}

static bool taws_printable_hex_string(const uint8_t *array, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (!taws_is_printable_char(array[i])) {
            return false;
        }
    }
    return true;
}

static teep_err_t taws_print_text_body(const char *text, size_t size)
{
    if (text == NULL && size > 0) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    if (size > TEEP_MAX_PRINT_BYTE_COUNT) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    char escaped_text[TEEP_MAX_PRINT_BYTE_COUNT * 2 + 1];
    size_t escaped_size = 0;
    for (size_t i = 0; i < size; i++) {
        if (text[i] == '\n') {
            escaped_text[escaped_size++] = '\\';
            escaped_text[escaped_size++] = 'n';
        } else {
            escaped_text[escaped_size++] = text[i];
        }
    }
    escaped_text[escaped_size] = '\0';
    printf("%s", escaped_text);
    return TEEP_SUCCESS;
}

static teep_err_t taws_print_text_within_max(const char *text, size_t size)
{
    if (text == NULL && size > 0) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    size_t print_size = (size <= TAWS_MAX_PRINT_TEXT_COUNT) ? size : TEEP_MAX_PRINT_TEXT_COUNT;
    printf("\"");
    teep_err_t result = taws_print_text_body(text, print_size);
    if (result != TEEP_SUCCESS) {
        return result;
    }
    printf("\"");
    if (size > TAWS_MAX_PRINT_TEXT_COUNT) {
        printf("..");
    }
    return TEEP_SUCCESS;
}

static teep_err_t taws_print_string(const teep_buf_t *string)
{
    if (string == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    if (string->ptr == NULL && string->len > 0) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    return taws_print_text_within_max((const char *)string->ptr, string->len);
}

static teep_err_t taws_print_hex_within_max(const uint8_t *array, size_t size)
{
    if (size == 0) {
        printf("''");
        return TEEP_SUCCESS;
    }
    if (array == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    size_t print_size = (size <= TEEP_MAX_PRINT_BYTE_COUNT) ? size : TEEP_MAX_PRINT_BYTE_COUNT;
    if (taws_printable_hex_string(array, print_size)) {
        printf("'");
        teep_err_t result = taws_print_text_body((const char *)array, print_size);
        if (result != TEEP_SUCCESS) {
            return result;
        }
        printf("'");
    } else {
        printf("h'");
        for (size_t i = 0; i < print_size; i++) {
            printf("%02x", (unsigned char)array[i]);
        }
        printf("'");
    }
    if (size > TEEP_MAX_PRINT_BYTE_COUNT) {
        printf("..");
    }
    return TEEP_SUCCESS;
}

static void taws_print_option_separator(bool *printed)
{
    if (*printed) {
        printf(",\n");
    }
    *printed = true;
}

static teep_err_t taws_print_option_token(const teep_buf_t *token,
                                          uint32_t indent_space,
                                          bool print_key)
{
    if (print_key) {
        printf("%*s/ token / %d : ", indent_space, "", TEEP_OPTIONS_KEY_TOKEN);
    } else {
        printf("%*s/ token : / ", indent_space, "");
    }
    return taws_print_hex_within_max(token->ptr, token->len);
}

static teep_err_t taws_print_option_attestation_payload_format(const teep_buf_t *format,
                                                               uint32_t indent_space)
{
    printf("%*s/ attestation-payload-format / %d : ", indent_space, "",
           TEEP_OPTIONS_KEY_ATTESTATION_PAYLOAD_FORMAT);
    return taws_print_string(format);
}

static teep_err_t taws_print_option_attestation_payload(const teep_buf_t *payload,
                                                        uint32_t indent_space)
{
    printf("%*s/ attestation-payload / %d : ", indent_space, "",
           TEEP_OPTIONS_KEY_ATTESTATION_PAYLOAD);
    return taws_print_hex_within_max(payload->ptr, payload->len);
}

static teep_err_t taws_print_option_suit_reports(const teep_buf_array_t *suit_reports,
                                                 uint32_t indent_space,
                                                 uint32_t indent_delta)
{
    printf("%*s/ suit-reports / %d : [\n", indent_space, "",
           TEEP_OPTIONS_KEY_SUIT_REPORTS);
    for (size_t i = 0; i < suit_reports->len; i++) {
        printf("%*s", indent_space + indent_delta, "");
        teep_err_t result = taws_print_hex_within_max(suit_reports->items[i].ptr,
                                                      suit_reports->items[i].len);
        if (result != TEEP_SUCCESS) {
            return result;
        }
        if (i + 1 < suit_reports->len) {
            printf(",\n");
        }
    }
    printf("\n%*s]", indent_space, "");
    return TEEP_SUCCESS;
}

static teep_err_t taws_print_option_unneeded_manifest_list(const teep_buf_array_t *component_ids,
                                                           uint32_t indent_space,
                                                           uint32_t indent_delta)
{
    printf("%*s/ unneeded-manifest-list / %d : [\n", indent_space, "",
           TEEP_OPTIONS_KEY_UNNEEDED_TC_LIST);
    for (size_t i = 0; i < component_ids->len; i++) {
        printf("%*s", indent_space + indent_delta, "");
        teep_err_t result = taws_print_component_id(&component_ids->items[i]);
        if (result != TEEP_SUCCESS) {
            return result;
        }
        if (i + 1 < component_ids->len) {
            printf(",\n");
        }
    }
    printf("\n%*s]", indent_space, "");
    return TEEP_SUCCESS;
}

static const char *taws_cose_mechanism_key_to_str(int64_t cose_mechanism_key)
{
    switch (cose_mechanism_key) {
    case CBOR_TAG_COSE_SIGN1:
        return "COSE_Sign1";
    case CBOR_TAG_SIGN:
        return "COSE_Sign";
    case CBOR_TAG_COSE_MAC0:
        return "COSE_Mac0";
    case CBOR_TAG_MAC:
        return "COSE_Mac";
    case CBOR_TAG_COSE_ENCRYPT0:
        return "COSE_Encrypt0";
    case CBOR_TAG_ENCRYPT:
        return "COSE_Encrypt";
    default:
        return NULL;
    }
}

static bool taws_is_valid_mechanism(int cose_tag)
{
    return taws_cose_mechanism_key_to_str(cose_tag) != NULL;
}

static teep_err_t taws_print_suit_cose_profile(const teep_suit_cose_profile_t *profile)
{
    if (profile == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    printf("[ %ld / %s /, %ld / %s /, %ld / %s /, %ld / %s / ]",
        profile->hash,
        teep_cose_algs_key_to_str(profile->hash),
        profile->authentication,
        teep_cose_algs_key_to_str(profile->authentication),
        profile->key_exchange,
        teep_cose_algs_key_to_str(profile->key_exchange),
        profile->encryption,
        teep_cose_algs_key_to_str(profile->encryption));
    return TEEP_SUCCESS;
}

static teep_err_t taws_print_cipher_suite(const teep_cipher_suite_t *cipher_suite)
{
    if (cipher_suite == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    printf("[");
    bool printed = false;
    for (size_t i = 0; i < TEEP_MAX_CIPHER_SUITES_LENGTH; i++) {
        if (!taws_is_valid_mechanism(cipher_suite->mechanisms[i].cose_tag)) {
            break;
        }
        if (printed) {
            printf(", ");
        }
        printf("[ / mechanism: / %d / (%s), / algorithm_id: / %d / (%s) / ]",
            cipher_suite->mechanisms[i].cose_tag,
            taws_cose_mechanism_key_to_str(cipher_suite->mechanisms[i].cose_tag),
            cipher_suite->mechanisms[i].algorithm_id,
            teep_cose_algs_key_to_str(cipher_suite->mechanisms[i].algorithm_id));
        printed = true;
    }
    printf("]");
    return TEEP_SUCCESS;
}

static void taws_print_data_item_requested(teep_data_item_requested_t requested)
{
    bool printed = false;
    if (requested.attestation) {
        printf("\"attestation\"");
        printed = true;
    }
    if (requested.trusted_components) {
        if (printed) {
            printf(" | ");
        }
        printf("\"trusted-components\"");
        printed = true;
    }
}

static teep_err_t taws_print_component_id(const teep_buf_t *component_id)
{
    if (component_id == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    return taws_print_hex_within_max(component_id->ptr, component_id->len);
}

static teep_err_t taws_print_query_request(const teep_query_request_t *query_request,
                                           uint32_t indent_space,
                                           uint32_t indent_delta)
{
    if (query_request == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }
    teep_err_t result = TEEP_SUCCESS;
    printf("%*s/ QueryRequest = / [\n", indent_space, "");
    printf("%*s/ type : / %u,\n", indent_space + indent_delta, "", query_request->type);
    printf("%*s/ options : / {\n", indent_space + indent_delta, "");
    bool printed = false;
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_TOKEN) {
        taws_print_option_separator(&printed);
        result = taws_print_option_token(&query_request->token,
                                         indent_space + 2 * indent_delta,
                                         false);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_SUPPORTED_FRESHNESS_MECHANISMS) {
        taws_print_option_separator(&printed);
        printf("%*s/ supported-freshness-mechanisms / %d : [ ", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_SUPPORTED_FRESHNESS_MECHANISMS);
        for (size_t i = 0; i < query_request->supported_freshness_mechanisms.len; i++) {
            printf("%u, ", query_request->supported_freshness_mechanisms.items[i]);
        }
        printf("]");
    }
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_CHALLENGE) {
        taws_print_option_separator(&printed);
        printf("%*s/ challenge / %d :", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_CHALLENGE);
        result = taws_print_hex_within_max(query_request->challenge.ptr, query_request->challenge.len);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_VERSIONS) {
        taws_print_option_separator(&printed);
        printf("%*s/ versions / %d :  [ ", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_VERSIONS);
        for (size_t i = 0; i < query_request->versions.len; i++) {
            printf("%u", query_request->versions.items[i]);
            if (i + 1 < query_request->versions.len) printf(", ");
        }
        printf(" ]");
    }
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD_FORMAT) {
        taws_print_option_separator(&printed);
        result = taws_print_option_attestation_payload_format(&query_request->attestation_payload_format,
                                                             indent_space + 2 * indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD) {
        taws_print_option_separator(&printed);
        result = taws_print_option_attestation_payload(&query_request->attestation_payload,
                                                       indent_space + 2 * indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_request->contains & TEEP_MESSAGE_CONTAINS_SUIT_REPORTS) {
        taws_print_option_separator(&printed);
        result = taws_print_option_suit_reports(&query_request->suit_reports,
                                                indent_space + 2 * indent_delta,
                                                indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    printf("\n%*s},\n", indent_space + indent_delta, "");
    printf("%*s/ supported-teep-cipher-suites : / [\n", indent_space + indent_delta, "");
    for (size_t i = 0; i < query_request->supported_teep_cipher_suites.len; i++) {
        printf("%*s", indent_space + 2 * indent_delta, "");
        result = taws_print_cipher_suite(&query_request->supported_teep_cipher_suites.items[i]);
        if (result != TEEP_SUCCESS) return result;
        if (i + 1 < query_request->supported_teep_cipher_suites.len) printf(",");
        printf("\n");
    }
    printf("%*s],\n", indent_space + indent_delta, "");
    printf("%*s/ supported-suit-cose-profiles : / [\n", indent_space + indent_delta, "");
    for (size_t i = 0; i < query_request->supported_suit_cose_profiles.len; i++) {
        printf("%*s", indent_space + 2 * indent_delta, "");
        result = taws_print_suit_cose_profile(&query_request->supported_suit_cose_profiles.items[i]);
        if (result != TEEP_SUCCESS) return result;
        if (i + 1 < query_request->supported_suit_cose_profiles.len) printf(",");
        printf("\n");
    }
    printf("%*s],\n", indent_space + indent_delta, "");
    printf("%*s/ data-item-requested : / %lu / (", indent_space + indent_delta, "", query_request->data_item_requested.val);
    taws_print_data_item_requested(query_request->data_item_requested);
    printf(") /\n");
    printf("%*s]\n", indent_space, "");
    return TEEP_SUCCESS;
}

static teep_err_t taws_print_query_response(const teep_query_response_t *query_response,
                                            uint32_t indent_space,
                                            uint32_t indent_delta)
{
    if (query_response == NULL) return TEEP_ERR_UNEXPECTED_ERROR;
    teep_err_t result = TEEP_SUCCESS;
    printf("%*s/ QueryResponse = / [\n", indent_space, "");
    printf("%*s/ type : / %u,\n", indent_space + indent_delta, "", query_response->type);
    printf("%*s/ options : / {\n", indent_space + indent_delta, "");
    bool printed = false;
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_TOKEN) {
        taws_print_option_separator(&printed);
        result = taws_print_option_token(&query_response->token,
                                         indent_space + 2 * indent_delta,
                                         true);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_SELECTED_VERSION) {
        taws_print_option_separator(&printed);
        printf("%*s/ selected-version / %d : %u",
               indent_space + 2 * indent_delta,
               "",
               TEEP_OPTIONS_KEY_SELECTED_VERSION,
               query_response->selected_version);
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD_FORMAT) {
        taws_print_option_separator(&printed);
        result = taws_print_option_attestation_payload_format(&query_response->attestation_payload_format,
                                                             indent_space + 2 * indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD) {
        taws_print_option_separator(&printed);
        result = taws_print_option_attestation_payload(&query_response->attestation_payload,
                                                       indent_space + 2 * indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_SUIT_REPORTS) {
        taws_print_option_separator(&printed);
        result = taws_print_option_suit_reports(&query_response->suit_reports,
                                                indent_space + 2 * indent_delta,
                                                indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_TC_LIST) {
        taws_print_option_separator(&printed);
        printf("%*s/ tc-list / %d : [", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_TC_LIST);
        for (size_t i = 0; i < query_response->tc_list.len; i++) {
            printf("\n%*s", indent_space + 3 * indent_delta, "");
            result = taws_print_hex_within_max(query_response->tc_list.items[i].ptr, query_response->tc_list.items[i].len);
            if (result != TEEP_SUCCESS) return result;
            if (i + 1 < query_response->tc_list.len) printf(",");
        }
        printf("\n%*s]", indent_space + 2 * indent_delta, "");
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_REQUESTED_TC_LIST) {
        taws_print_option_separator(&printed);
        printf("%*s/ requested-tc-list / %d : [\n", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_REQUESTED_TC_LIST);
        for (size_t i = 0; i < query_response->requested_tc_list.len; i++) {
            printf("%*s{\n", indent_space + 3 * indent_delta, "");
            printf("%*s/ component-id / %d : ", indent_space + 4 * indent_delta, "", TEEP_OPTIONS_KEY_COMPONENT_ID);
            result = taws_print_component_id(&query_response->requested_tc_list.items[i].component_id);
            if (result != TEEP_SUCCESS) return result;
            if (query_response->requested_tc_list.items[i].contains & TEEP_MESSAGE_CONTAINS_TC_MANIFEST_SEQUENCE_NUMBER) {
                printf(",\n%*s/ tc-manifest-sequence-number / %d : %lu", indent_space + 4 * indent_delta, "", TEEP_OPTIONS_KEY_TC_MANIFEST_SEQUENCE_NUMBER, query_response->requested_tc_list.items[i].tc_manifest_sequence_number);
            }
            if (query_response->requested_tc_list.items[i].contains & TEEP_MESSAGE_CONTAINS_HAVE_BINARY) {
                printf(",\n%*s/ have-binary / %d : %s", indent_space + 4 * indent_delta, "", TEEP_OPTIONS_KEY_HAVE_BINARY, query_response->requested_tc_list.items[i].have_binary ? "true" : "false");
            }
            printf("\n%*s}\n", indent_space + 3 * indent_delta, "");
        }
        printf("%*s]", indent_space + 2 * indent_delta, "");
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_UNNEEDED_TC_LIST) {
        taws_print_option_separator(&printed);
        result = taws_print_option_unneeded_manifest_list(&query_response->unneeded_manifest_list,
                                                          indent_space + 2 * indent_delta,
                                                          indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (query_response->contains & TEEP_MESSAGE_CONTAINS_EXT_LIST) {
        taws_print_option_separator(&printed);
        printf("%*sext-list : [", indent_space + 2 * indent_delta, "");
        for (size_t i = 0; i < query_response->ext_list.len; i++) {
            printf("%lu ", query_response->ext_list.items[i]);
        }
        printf("]");
    }
    printf("\n%*s}\n%*s]\n", indent_space + indent_delta, "", indent_space, "");
    return TEEP_SUCCESS;
}

static teep_err_t taws_print_update(const teep_update_t *teep_update,
                                    uint32_t indent_space,
                                    uint32_t indent_delta,
                                    const unsigned char *ta_public_key)
{
    (void)ta_public_key;
    if (teep_update == NULL) return TEEP_ERR_UNEXPECTED_ERROR;
    teep_err_t result = TEEP_SUCCESS;
    printf("%*s/ Update = / [\n", indent_space, "");
    printf("%*s/ type : / %u,\n", indent_space + indent_delta, "", teep_update->type);
    printf("%*s/ options : {\n", indent_space + indent_delta, "");
    bool printed = false;
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_TOKEN) {
        taws_print_option_separator(&printed);
        result = taws_print_option_token(&teep_update->token,
                                         indent_space + 2 * indent_delta,
                                         true);
        if (result != TEEP_SUCCESS) return result;
    }
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_UNNEEDED_TC_LIST) {
        taws_print_option_separator(&printed);
        result = taws_print_option_unneeded_manifest_list(&teep_update->unneeded_manifest_list,
                                                          indent_space + 2 * indent_delta,
                                                          indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_MANIFEST_LIST) {
        taws_print_option_separator(&printed);
        printf("%*s/ manifest-list / %d : [\n", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_MANIFEST_LIST);
        for (size_t i = 0; i < teep_update->manifest_list.len; i++) {
            printf("%*s", indent_space + 3 * indent_delta, "");
            result = taws_print_hex_within_max(teep_update->manifest_list.items[i].ptr, teep_update->manifest_list.items[i].len);
            if (result != TEEP_SUCCESS) return result;
            if (i + 1 < teep_update->manifest_list.len) printf(",");
            printf("\n");
        }
        printf("%*s]", indent_space + 2 * indent_delta, "");
    }
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD_FORMAT) {
        taws_print_option_separator(&printed);
        result = taws_print_option_attestation_payload_format(&teep_update->attestation_payload_format,
                                                             indent_space + 2 * indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_ATTESTATION_PAYLOAD) {
        taws_print_option_separator(&printed);
        result = taws_print_option_attestation_payload(&teep_update->attestation_payload,
                                                       indent_space + 2 * indent_delta);
        if (result != TEEP_SUCCESS) return result;
    }
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_ERR_CODE) {
        taws_print_option_separator(&printed);
        printf("%*s/ err-code / %d : %u / %s /", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_ERR_CODE, teep_update->err_code, teep_err_code_to_str(teep_update->err_code));
    }
    if (teep_update->contains & TEEP_MESSAGE_CONTAINS_ERR_MSG) {
        taws_print_option_separator(&printed);
        printf("%*s/ err-msg / %d : ", indent_space + 2 * indent_delta, "", TEEP_OPTIONS_KEY_ERR_MSG);
        result = taws_print_string(&teep_update->err_msg);
        if (result != TEEP_SUCCESS) return result;
    }
    printf("\n%*s}\n%*s]\n", indent_space + indent_delta, "", indent_space, "");
    return TEEP_SUCCESS;
}

extern "C" teep_err_t taws_teep_dump_message(const teep_message_t *message,
                                             uint32_t indent_space,
                                             uint32_t indent_delta,
                                             const unsigned char *ta_public_key)
{
    (void)ta_public_key;
    if (message == NULL) {
        return TEEP_ERR_UNEXPECTED_ERROR;
    }

    teep_err_t result = TEEP_SUCCESS;
    switch (message->teep_message.type) {
    case TEEP_TYPE_QUERY_REQUEST:
        result = taws_print_query_request(&message->query_request, indent_space, indent_delta);
        break;
    case TEEP_TYPE_QUERY_RESPONSE:
        result = taws_print_query_response(&message->query_response, indent_space, indent_delta);
        break;
    case TEEP_TYPE_UPDATE:
        result = taws_print_update(&message->teep_update, indent_space, indent_delta, ta_public_key);
        break;
    case TEEP_TYPE_SUCCESS:
        result = teep_print_success(&message->teep_success, indent_space, indent_delta);
        break;
    case TEEP_TYPE_ERROR:
        result = teep_print_error(&message->teep_error, indent_space, indent_delta);
        break;
    default:
        result = TEEP_ERR_INVALID_MESSAGE_TYPE;
        break;
    }
    printf("\n");
    return result;
}
