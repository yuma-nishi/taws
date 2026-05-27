/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Enclave.h"
#include "debug_print.h"
#include "tam_esp256_public_key.h"
#include "teep_agent_esp256_private_key.h"
#include "teep_agent_esp256_public_key.h"

#include <stdio.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include "qcbor/UsefulBuf.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "teep/teep_message_data.h"
#include "teep/teep_message_print.h"
#include "teep/teep_common.h"
#include "teep/teep_cose.h"

teep_err_t teep_generate_sha256(UsefulBufC target, UsefulBuf hash);

#ifdef __cplusplus
}
#endif

/*Buffer size for ESP256*/
#define COSE_KEY_THUMBPRINT_BUFFER_SIZE (11+32+32)

/* Mechanism key*/
teep_mechanism_t agent_sign = {};
teep_mechanism_t tam_verify = {};
teep_key_state_t g_key_state = TEEP_KEY_UNINITIALIZED;
static bool g_agent_key_owns_private = false;
static bool g_agent_key_owns_public = false;
static bool g_agent_key_owns_kid = false;


//stub functions
extern "C" int sprintf(char *buf, const char *fmt, ...)
{
    (void)buf;
    (void)fmt;
    return 0;
}

static void clear_agent_sign_buffers(void)
{
    if (g_agent_key_owns_private && agent_sign.key.private_key != NULL) {
        free(const_cast<void *>(
            static_cast<const void *>(agent_sign.key.private_key)));
        agent_sign.key.private_key = NULL;
        agent_sign.key.private_key_len = 0;
    }
    if (g_agent_key_owns_public && agent_sign.key.public_key != NULL) {
        free(const_cast<void *>(
            static_cast<const void *>(agent_sign.key.public_key)));
        agent_sign.key.public_key = NULL;
        agent_sign.key.public_key_len = 0;
    }
    if (g_agent_key_owns_kid && agent_sign.key.kid.ptr != NULL) {
        free(const_cast<void *>(
            static_cast<const void *>(agent_sign.key.kid.ptr)));
        agent_sign.key.kid.ptr = NULL;
        agent_sign.key.kid.len = 0;
    }
    g_agent_key_owns_private = false;
    g_agent_key_owns_public = false;
    g_agent_key_owns_kid = false;
}

/*!
    \brief      Genearte a KID and store it in key_pair->kid

    \param[in,out]  key_pair         key pair.

    \return     This returns one of error codes defined by \ref teep_err_t;
 */
teep_err_t teep_generate_kid(teep_key_t *key) {
    
    teep_err_t          result;
    QCBOREncodeContext encode_context;
    UsefulBufC cose_key_bytes;
   
    UsefulBuf_MAKE_STACK_UB(buf, COSE_KEY_THUMBPRINT_BUFFER_SIZE);

    QCBOREncode_Init(&encode_context, buf);
    QCBOREncode_OpenMap(&encode_context);
    QCBOREncode_AddInt64ToMapN(&encode_context, TEEP_COSE_KTY, TEEP_COSE_KTY_EC2);
    QCBOREncode_AddInt64ToMapN(&encode_context, TEEP_COSE_CRV, TEEP_COSE_CRV_P256);
    QCBOREncode_AddBytesToMapN(&encode_context, TEEP_COSE_X, (UsefulBufC){.ptr = key->public_key+1, .len = 32});
    QCBOREncode_AddBytesToMapN(&encode_context, TEEP_COSE_Y, (UsefulBufC){.ptr = key->public_key+33, .len = 32});
    QCBOREncode_CloseMap(&encode_context);
    QCBOREncode_Finish(&encode_context, &cose_key_bytes);

    key->kid.len = SHA256_DIGEST_LENGTH;
    key->kid.ptr  = malloc(SHA256_DIGEST_LENGTH);

    UsefulBuf kid_buf = { const_cast<void *>(key->kid.ptr), key->kid.len };
    result = teep_generate_sha256(cose_key_bytes, kid_buf);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("create_evidence_generic : Failed to calc cose key thumbprint. %s(%d)\n", teep_err_to_str(result), result);
        return result;
    }

    return result;
}

/*!
    \brief      Generate key pair and set them to pair_key.

    \param[in,out]  key_pair         key pair.

    \return     This returns one of error codes defined by \ref teep_err_t;
 */
extern "C" teep_err_t ecall_teep_generate_esp256_key_pair() {
    teep_err_t ret = TEEP_ERR_UNEXPECTED_ERROR;

    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;
    EC_KEY *ec_key = NULL;
    const EC_POINT *pub_point = NULL;
    const BIGNUM *priv_bn = NULL;
    const EC_GROUP *group = NULL;

    static unsigned char priv_bytes[32];
    static unsigned char pub_bytes[65]; // 0x04 + X(32) + Y(32)
    size_t pub_len = sizeof(pub_bytes);

    memset(priv_bytes, 0, sizeof(priv_bytes));
    memset(pub_bytes, 0, sizeof(pub_bytes));

    clear_agent_sign_buffers();
    (void)teep_free_key(&agent_sign.key);
    //g_key_state is now TEEP_KEY_UNINITIALIZED

    /* generate key pair */
    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) goto err;

    if (EVP_PKEY_keygen_init(pctx) <= 0) goto err;
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) <= 0) goto err;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) goto err;

    #if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #endif
    ec_key = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec_key) goto err;
    group = EC_KEY_get0_group(ec_key);
    pub_point = EC_KEY_get0_public_key(ec_key);
    priv_bn = EC_KEY_get0_private_key(ec_key);

    /* convert bignum to byte */
    BN_bn2binpad(priv_bn, priv_bytes, 32);

    /* fetch public key */
    pub_len = EC_POINT_point2oct(group, pub_point, POINT_CONVERSION_UNCOMPRESSED,
                                pub_bytes, sizeof(pub_bytes), NULL);
    (void)pub_len;

    /* set the public key to key pair */
    ret = teep_key_init_esp256_key_pair(priv_bytes, pub_bytes, NULLUsefulBufC, &agent_sign.key);
    if(ret != TEEP_SUCCESS){
        PRINT_DEBUG_LOG("create_evidence_generic : Failed to create cose key. %s(%d)\n", teep_err_to_str(ret), ret);
        goto err;
    }

    agent_sign.key.private_key = priv_bytes;
    agent_sign.key.private_key_len = PRIME256V1_PRIVATE_KEY_LENGTH;
    g_agent_key_owns_private = true;

    agent_sign.key.public_key = pub_bytes;
    agent_sign.key.public_key_len = PRIME256V1_PUBLIC_KEY_LENGTH;
    g_agent_key_owns_public = true;


    ret = teep_generate_kid(&agent_sign.key);
    if(ret != TEEP_SUCCESS){
        PRINT_DEBUG_LOG("create_evidence_generic : Failed to calc cose key thumbprint. %s(%d)\n", teep_err_to_str(ret), ret);
        goto err;
    }
    g_agent_key_owns_kid = true;

    /* setting tam_esp256_public_key */
    ret = teep_key_init_esp256_public_key(tam_esp256_public_key, NULLUsefulBufC, &tam_verify.key);
    if (ret != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to parse t_cose public key. %s(%d)\n", teep_err_to_str(ret), ret);
        goto err;
    }
    tam_verify.cose_tag = CBOR_TAG_COSE_SIGN1;
    g_key_state = TEEP_KEY_READY;



    goto cleanup;

err:
    clear_agent_sign_buffers();
    g_key_state = TEEP_KEY_UNINITIALIZED;

cleanup:
    EC_KEY_free(ec_key);
    #if defined(__GNUC__)
    #pragma GCC diagnostic pop
    #endif
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);

    return ret;
}

extern "C" void ecall_teep_free_keypair(){
    if (g_key_state == TEEP_KEY_UNINITIALIZED) {
        return;
    }
    clear_agent_sign_buffers();
    teep_free_key(&agent_sign.key);
    teep_free_key(&tam_verify.key);
    agent_sign = {};
    tam_verify = {};
    g_key_state = TEEP_KEY_UNINITIALIZED;
}


extern "C" teep_err_t ecall_teep_set_esp256_key(){
    teep_err_t result = TEEP_ERR_UNEXPECTED_ERROR;

    clear_agent_sign_buffers();
    (void)teep_free_key(&agent_sign.key);
    g_key_state = TEEP_KEY_UNINITIALIZED;
    result = teep_key_init_esp256_key_pair(teep_agent_esp256_private_key, teep_agent_esp256_public_key, NULLUsefulBufC, &agent_sign.key);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to set key pair. %s(%d)\n", teep_err_to_str(result), result);
        return result;
    }   
    result = teep_generate_kid(&agent_sign.key);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to create kid. %s(%d)\n", teep_err_to_str(result), result);
        return result;
    }
    g_agent_key_owns_kid = true;

    /* setting tam_esp256_public_key */
    result = teep_key_init_esp256_public_key(tam_esp256_public_key, NULLUsefulBufC, &tam_verify.key);
    if (result != TEEP_SUCCESS) {
        PRINT_DEBUG_LOG("main : Failed to parse t_cose public key. %s(%d)\n", teep_err_to_str(result), result);
        return result;
    }
    tam_verify.cose_tag = CBOR_TAG_COSE_SIGN1;
    g_key_state = TEEP_KEY_READY;

    return result;
}
