/*
 * signature.c - sign/verify an ISO's checksum using OpenSSL EVP.
 * Copyright (C) 2001-2024 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * signature.c - create and verify a detached cryptographic signature over an
 * ISO's embedded md5 checksum.
 *
 * WHAT IS SIGNED
 *   The message is the 32-character md5 hex STRING that implantisomd5 embeds
 *   in the image (e.g. "fbac254c...f8ad"). Signing a short, stable identifier
 *   rather than the whole multi-gigabyte image keeps signing/verifying cheap,
 *   while the md5 media check (libcheckisomd5.c) still guarantees the image
 *   content matches that identifier. The two checks are complementary:
 *     - md5 media check  -> image content is intact (integrity)
 *     - signature check  -> the md5 was blessed by the key holder (authenticity)
 *
 * WHY DETACHED (a separate "<iso>.sig" file)
 *   The PVD application-data area is only 512 bytes and already mostly full;
 *   an RSA-2048 signature alone is 256 bytes (512 hex / 344 base64 chars) and
 *   would not fit. A detached file has no size limit and works for any key
 *   type. The trade-off is that the .sig must travel WITH the image.
 *
 * KEY TYPES
 *   Uses the OpenSSL EVP one-shot interface (EVP_DigestSign/EVP_DigestVerify),
 *   which transparently supports RSA, ECDSA and EdDSA. The digest is SHA-256,
 *   except for Ed25519/Ed448 which sign the message directly and must be
 *   driven with a NULL digest (see is_oneshot_key()).
 *
 * ENCODING
 *   The raw signature bytes are base64-encoded (EVP_EncodeBlock) so the .sig
 *   file is plain ASCII and easy to transport.
 *
 * SCOPE / LIMITATIONS
 *   Verification only uses the PUBLIC KEY inside the supplied certificate. It
 *   does NOT validate the certificate's trust chain, validity dates or
 *   revocation - do that separately (e.g. "openssl verify") if required.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "signature.h"

/* Maximum size we are willing to read for a (base64) detached signature. */
#define SIG_MAX_B64 8192

/*
 * Some key types (Ed25519 / Ed448) require the "one-shot" interface and do not
 * take a separate digest. Detect those so we can select the correct API.
 */
static int is_oneshot_key(EVP_PKEY *const pkey) {
    const int id = EVP_PKEY_base_id(pkey);
    return id == EVP_PKEY_ED25519 || id == EVP_PKEY_ED448;
}

static EVP_PKEY *load_private_key(const char *const path) {
    BIO *bio = BIO_new_file(path, "rb");
    if (bio == NULL)
        return NULL;
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return pkey;
}

static X509 *load_certificate(const char *const path) {
    BIO *bio = BIO_new_file(path, "rb");
    if (bio == NULL)
        return NULL;
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (cert == NULL) {
        if (BIO_reset(bio) == 0)
            cert = d2i_X509_bio(bio, NULL);
    }
    BIO_free(bio);
    return cert;
}

/* Base64-encode src into a newly malloc'd NUL-terminated string (no newlines). */
static char *base64_encode(const unsigned char *const src, const size_t len) {
    const size_t outcap = 4 * ((len + 2) / 3) + 1;
    char *out = malloc(outcap);
    if (out == NULL)
        return NULL;
    const int n = EVP_EncodeBlock((unsigned char *) out, src, (int) len);
    if (n < 0) {
        free(out);
        return NULL;
    }
    out[n] = '\0';
    return out;
}

/* Base64-decode a NUL-terminated string into *out (malloc'd). Returns length or -1. */
static long base64_decode(const char *const src, unsigned char **out) {
    const size_t srclen = strlen(src);
    if (srclen == 0 || srclen % 4 != 0)
        return -1;
    unsigned char *buf = malloc(srclen / 4 * 3 + 1);
    if (buf == NULL)
        return -1;
    const int n = EVP_DecodeBlock(buf, (const unsigned char *) src, (int) srclen);
    if (n < 0) {
        free(buf);
        return -1;
    }
    /* EVP_DecodeBlock counts padding '=' as decoded zero bytes; adjust. */
    long real = n;
    for (size_t i = srclen; i > 0 && src[i - 1] == '='; i--)
        real--;
    *out = buf;
    return real;
}

/*
 * Sign msg[0..msglen) with the PEM private key at keypath and write a base64
 * detached signature to sigpath. See signature.h for the full contract.
 * The whole operation is self-contained: load key -> EVP sign -> base64 ->
 * write file, freeing every resource on all paths via the "done" label.
 */
int signature_sign_file(const char *const keypath, const unsigned char *const msg,
                         const size_t msglen, const char *const sigpath, char **errstr) {
    int rc = -1;
    EVP_PKEY *pkey = load_private_key(keypath);
    if (pkey == NULL) {
        if (errstr)
            *errstr = "Unable to read signing private key.";
        return -1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        if (errstr)
            *errstr = "Out of memory creating signing context.";
        EVP_PKEY_free(pkey);
        return -1;
    }

    unsigned char *sig = NULL;
    size_t siglen = 0;
    const EVP_MD *md = is_oneshot_key(pkey) ? NULL : EVP_sha256();

    if (EVP_DigestSignInit(ctx, NULL, md, NULL, pkey) != 1) {
        if (errstr)
            *errstr = "Unable to initialize signing operation.";
        goto done;
    }

    /* One-shot interface works for RSA, ECDSA and EdDSA keys. */
    if (EVP_DigestSign(ctx, NULL, &siglen, msg, msglen) != 1 || siglen == 0) {
        if (errstr)
            *errstr = "Unable to determine signature size.";
        goto done;
    }
    sig = malloc(siglen);
    if (sig == NULL) {
        if (errstr)
            *errstr = "Out of memory allocating signature.";
        goto done;
    }
    if (EVP_DigestSign(ctx, sig, &siglen, msg, msglen) != 1) {
        if (errstr)
            *errstr = "Signing failed.";
        goto done;
    }

    char *b64 = base64_encode(sig, siglen);
    if (b64 == NULL) {
        if (errstr)
            *errstr = "Unable to encode signature.";
        goto done;
    }

    FILE *fp = fopen(sigpath, "w");
    if (fp == NULL) {
        if (errstr)
            *errstr = "Unable to write signature file.";
        free(b64);
        goto done;
    }
    fprintf(fp, "%s\n", b64);
    fclose(fp);
    free(b64);
    rc = 0;

done:
    free(sig);
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

/*
 * Verify the base64 detached signature at sigpath over msg[0..msglen) using
 * the public key from the certificate at certpath. Returns an
 * enum signature_status value (VALID / INVALID / MISSING / ERROR). A missing
 * .sig file is distinguished from a verification failure so callers can report
 * the two cases differently.
 */
int signature_verify_file(const char *const certpath, const unsigned char *const msg,
                          const size_t msglen, const char *const sigpath, char **errstr) {
    /* Read the detached signature file. Absence is a distinct status. */
    FILE *fp = fopen(sigpath, "r");
    if (fp == NULL)
        return SIGNATURE_MISSING;

    char b64[SIG_MAX_B64];
    size_t n = fread(b64, 1, sizeof(b64) - 1, fp);
    fclose(fp);
    b64[n] = '\0';
    /* Strip trailing whitespace/newlines. */
    while (n > 0 && (b64[n - 1] == '\n' || b64[n - 1] == '\r' ||
                     b64[n - 1] == ' ' || b64[n - 1] == '\t'))
        b64[--n] = '\0';

    unsigned char *sig = NULL;
    const long siglen = base64_decode(b64, &sig);
    if (siglen <= 0) {
        if (errstr)
            *errstr = "Signature file is malformed.";
        return SIGNATURE_ERROR;
    }

    X509 *cert = load_certificate(certpath);
    if (cert == NULL) {
        if (errstr)
            *errstr = "Unable to read certificate.";
        free(sig);
        return SIGNATURE_ERROR;
    }

    EVP_PKEY *pkey = X509_get_pubkey(cert);
    if (pkey == NULL) {
        if (errstr)
            *errstr = "Certificate has no usable public key.";
        X509_free(cert);
        free(sig);
        return SIGNATURE_ERROR;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        if (errstr)
            *errstr = "Out of memory creating verify context.";
        EVP_PKEY_free(pkey);
        X509_free(cert);
        free(sig);
        return SIGNATURE_ERROR;
    }

    const EVP_MD *md = is_oneshot_key(pkey) ? NULL : EVP_sha256();
    int result = SIGNATURE_ERROR;

    if (EVP_DigestVerifyInit(ctx, NULL, md, NULL, pkey) != 1) {
        if (errstr)
            *errstr = "Unable to initialize verification.";
    } else {
        const int v = EVP_DigestVerify(ctx, sig, (size_t) siglen, msg, msglen);
        if (v == 1)
            result = SIGNATURE_VALID;
        else if (v == 0)
            result = SIGNATURE_INVALID;
        else {
            if (errstr)
                *errstr = "Signature verification error.";
            result = SIGNATURE_ERROR;
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    X509_free(cert);
    free(sig);
    return result;
}
