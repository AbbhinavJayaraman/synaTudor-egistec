//Hash algorithm providers for the BCrypt shim.
//
//The Tudor driver never needed these - it hashed through the older
//wincrypt/EVP path - so bcrypt_algos[] only ever held AES and the two P256
//curves. EgisTouchFPEngine0575.dll opens SHA256 through BCrypt, which made
//BCryptOpenAlgorithmProvider fail outright.
//
//Everything here is backed by OpenSSL EVP, which libtudor already links.

#include <string.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include "bcrypt.h"

struct bcrypt_hash {
    const struct bcrypt_hash_algorithm *algo;
    EVP_MD_CTX *md_ctx;
    //HMAC goes through the OpenSSL 3 EVP_MAC interface; the old HMAC_CTX API
    //is deprecated there and warns on any recent distro.
    EVP_MAC *mac;
    EVP_MAC_CTX *mac_ctx;
};

const EVP_MD *bcrypt_hash_algo_md(const struct bcrypt_hash_algorithm *algo) {
    const EVP_MD *md = EVP_get_digestbyname(algo->evp_name);
    if(!md) {
        log_error("libcrypto doesn't know digest '%s'!", algo->evp_name);
        return NULL;
    }
    return md;
}

bool bcrypt_algo_is_hash(const struct bcrypt_algorithm *algo) {
    return algo && algo->is_hash;
}

//A secret of non-zero length selects HMAC, matching BCryptCreateHash's
//contract when the provider was opened with BCRYPT_ALG_HANDLE_HMAC_FLAG.
NTSTATUS bcrypt_hash_create(const struct bcrypt_hash_algorithm *algo, const void *secret, size_t secret_size, struct bcrypt_hash **out) {
    const EVP_MD *md = bcrypt_hash_algo_md(algo);
    if(!md) return WINERR_SET_CODE;

    struct bcrypt_hash *hash = (struct bcrypt_hash*) malloc(sizeof(struct bcrypt_hash));
    if(!hash) return WINERR_SET_CODE;
    *hash = (struct bcrypt_hash) { .algo = algo };

    if(secret && secret_size > 0) {
        hash->mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
        if(!hash->mac) { free(hash); return WINERR_SET_CODE; }

        hash->mac_ctx = EVP_MAC_CTX_new(hash->mac);
        if(!hash->mac_ctx) { EVP_MAC_free(hash->mac); free(hash); return WINERR_SET_CODE; }

        OSSL_PARAM params[] = {
            OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, (char*) algo->evp_name, 0),
            OSSL_PARAM_construct_end()
        };
        if(EVP_MAC_init(hash->mac_ctx, (const unsigned char*) secret, secret_size, params) <= 0) {
            EVP_MAC_CTX_free(hash->mac_ctx);
            EVP_MAC_free(hash->mac);
            free(hash);
            return WINERR_SET_CODE;
        }
    } else {
        hash->md_ctx = EVP_MD_CTX_new();
        if(!hash->md_ctx) { free(hash); return WINERR_SET_CODE; }
        if(EVP_DigestInit_ex(hash->md_ctx, md, NULL) <= 0) {
            EVP_MD_CTX_free(hash->md_ctx);
            free(hash);
            return WINERR_SET_CODE;
        }
    }

    *out = hash;
    return STATUS_SUCCESS;
}

NTSTATUS bcrypt_hash_update(struct bcrypt_hash *hash, const void *data, size_t size) {
    if(!hash) return WINERR_SET_CODE;
    if(!data || size == 0) return STATUS_SUCCESS;

    if(hash->mac_ctx) {
        if(EVP_MAC_update(hash->mac_ctx, (const unsigned char*) data, size) <= 0) return WINERR_SET_CODE;
    } else {
        if(EVP_DigestUpdate(hash->md_ctx, data, size) <= 0) return WINERR_SET_CODE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS bcrypt_hash_finish(struct bcrypt_hash *hash, void *out, size_t out_size) {
    if(!hash || !out) return WINERR_SET_CODE;
    if(out_size < hash->algo->digest_size) return WINERR_SET_CODE;

    unsigned char digest[EVP_MAX_MD_SIZE];
    size_t len = 0;

    if(hash->mac_ctx) {
        if(EVP_MAC_final(hash->mac_ctx, digest, &len, sizeof(digest)) <= 0) return WINERR_SET_CODE;
    } else {
        unsigned int mdlen = 0;
        if(EVP_DigestFinal_ex(hash->md_ctx, digest, &mdlen) <= 0) return WINERR_SET_CODE;
        len = mdlen;
    }

    if(len > out_size) len = out_size;
    memcpy(out, digest, len);
    return STATUS_SUCCESS;
}

void bcrypt_hash_destroy(struct bcrypt_hash *hash) {
    if(!hash) return;
    if(hash->mac_ctx) EVP_MAC_CTX_free(hash->mac_ctx);
    if(hash->mac) EVP_MAC_free(hash->mac);
    if(hash->md_ctx) EVP_MD_CTX_free(hash->md_ctx);
    free(hash);
}

#define HASH_ALGO(sym, winname, evpname, dsize) \
    struct bcrypt_hash_algorithm sym = { \
        .algo = { \
            .name = (winname), \
            .is_hash = true, \
        }, \
        .evp_name = (evpname), \
        .digest_size = (dsize) \
    }

HASH_ALGO(bcrypt_algo_sha1,   "SHA1",   "SHA1",   20);
HASH_ALGO(bcrypt_algo_sha256, "SHA256", "SHA256", 32);
HASH_ALGO(bcrypt_algo_sha384, "SHA384", "SHA384", 48);
HASH_ALGO(bcrypt_algo_sha512, "SHA512", "SHA512", 64);
HASH_ALGO(bcrypt_algo_md5,    "MD5",    "MD5",    16);
