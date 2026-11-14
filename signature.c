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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
 * WHERE THE SIGNATURE LIVES
 *   The PVD application-data area is only 512 bytes and already mostly full;
 *   an RSA-2048 signature alone is 256 bytes (344 base64 chars) and would not
 *   fit. So the signature is stored in its own 2 KiB sector in the trailing
 *   SKIPSECTORS region of the ISO (see signature_sign_block /
 *   signature_verify_block). That region is excluded from the md5, so the
 *   embedded signature does not change the md5 it covers, and the signature
 *   travels inside the image with no detached file. The older file-based
 *   helpers (signature_sign_file / signature_verify_file) remain for callers
 *   that still want a detached "<iso>.sig".
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
 *   signature_verify_file() only uses the PUBLIC KEY inside the supplied
 *   certificate; it does NOT by itself validate the certificate's trust chain
 *   or validity dates. Use signature_verify_chain() (which wraps OpenSSL's
 *   X509_verify_cert) to confirm the certificate chains to a trusted CA before
 *   trusting its key. Neither function checks revocation (CRL/OCSP).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "signature.h"
#include "utilities.h" /* SECTOR_SIZE */

/* Maximum size we are willing to read for a (base64) detached signature. */
#define SIG_MAX_B64 8192

/*
 * Embedded-signature block format.
 *
 * Instead of a detached "<iso>.sig" file, the base64 signature is stored in a
 * single 2 KiB sector INSIDE the ISO, in the trailing SKIPSECTORS region that
 * is excluded from the md5 (so writing it does not change the md5 the
 * signature covers). The block is:
 *
 *   "ISOMD5-SIG-v1\n" <base64 signature> "\n" <NUL padding to SECTOR_SIZE>
 *
 * The magic line lets the reader distinguish a real signature block from the
 * all-zero padding that is normally present in the skipped sectors.
 */
#define SIG_BLOCK_MAGIC "ISOMD5-SIG-v1\n"
#define SIG_BLOCK_SIZE  ((size_t) SECTOR_SIZE)

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
 * Sign msg[0..msglen) with the PEM private key at keypath and return the
 * base64-encoded signature as a newly malloc'd NUL-terminated string (no
 * newline). Returns NULL on failure and sets *errstr. The caller frees the
 * result. This is the shared core used by both the detached-file and the
 * embedded-block signers.
 */
static char *sign_to_b64(const char *const keypath, const unsigned char *const msg,
                         const size_t msglen, char **errstr) {
    char *b64 = NULL;
    EVP_PKEY *pkey = load_private_key(keypath);
    if (pkey == NULL) {
        if (errstr)
            *errstr = "Unable to read signing private key.";
        return NULL;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        if (errstr)
            *errstr = "Out of memory creating signing context.";
        EVP_PKEY_free(pkey);
        return NULL;
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

    b64 = base64_encode(sig, siglen);
    if (b64 == NULL && errstr)
        *errstr = "Unable to encode signature.";

done:
    free(sig);
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return b64;
}

/*
 * Sign msg[0..msglen) with the PEM private key at keypath and write a base64
 * detached signature to sigpath. See signature.h for the full contract.
 */
int signature_sign_file(const char *const keypath, const unsigned char *const msg,
                         const size_t msglen, const char *const sigpath, char **errstr) {
    char *b64 = sign_to_b64(keypath, msg, msglen, errstr);
    if (b64 == NULL)
        return -1;

    FILE *fp = fopen(sigpath, "w");
    if (fp == NULL) {
        if (errstr)
            *errstr = "Unable to write signature file.";
        free(b64);
        return -1;
    }
    fprintf(fp, "%s\n", b64);
    fclose(fp);
    free(b64);
    return 0;
}

/*
 * Sign msg[0..msglen) and write the signature as a magic-prefixed block into
 * sector `sector` of the open ISO file descriptor isofd. See signature.h.
 */
int signature_sign_block(const char *const keypath, const unsigned char *const msg,
                         const size_t msglen, const int isofd, const off_t sector,
                         char **errstr) {
    char *b64 = sign_to_b64(keypath, msg, msglen, errstr);
    if (b64 == NULL)
        return -1;

    const size_t magiclen = strlen(SIG_BLOCK_MAGIC);
    const size_t b64len = strlen(b64);
    /* magic + base64 + trailing newline must fit in one sector. */
    if (magiclen + b64len + 1 > SIG_BLOCK_SIZE) {
        if (errstr)
            *errstr = "Signature too large for one sector.";
        free(b64);
        return -1;
    }

    unsigned char *block = calloc(1, SIG_BLOCK_SIZE);
    if (block == NULL) {
        if (errstr)
            *errstr = "Out of memory building signature block.";
        free(b64);
        return -1;
    }
    memcpy(block, SIG_BLOCK_MAGIC, magiclen);
    memcpy(block + magiclen, b64, b64len);
    block[magiclen + b64len] = '\n';
    free(b64);

    const off_t off = sector * (off_t) SECTOR_SIZE;
    if (lseek(isofd, off, SEEK_SET) < 0) {
        if (errstr)
            *errstr = "Seek to signature sector failed.";
        free(block);
        return -1;
    }
    ssize_t w = write(isofd, block, SIG_BLOCK_SIZE);
    free(block);
    if (w != (ssize_t) SIG_BLOCK_SIZE) {
        if (errstr)
            *errstr = "Writing signature block failed.";
        return -1;
    }
    return 0;
}

/*
 * Verify a base64 signature string (b64) over msg[0..msglen) using the public
 * key from the certificate at certpath. Shared core for the detached-file and
 * embedded-block verifiers. Returns an enum signature_status value.
 */
static int verify_b64(const char *const certpath, const unsigned char *const msg,
                      const size_t msglen, const char *const b64, char **errstr) {
    unsigned char *sig = NULL;
    const long siglen = base64_decode(b64, &sig);
    if (siglen <= 0) {
        if (errstr)
            *errstr = "Signature is malformed.";
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

/*
 * Verify the base64 detached signature at sigpath over msg[0..msglen) using
 * the public key from the certificate at certpath. A missing .sig file is
 * reported as SIGNATURE_MISSING.
 */
int signature_verify_file(const char *const certpath, const unsigned char *const msg,
                          const size_t msglen, const char *const sigpath, char **errstr) {
    FILE *fp = fopen(sigpath, "r");
    if (fp == NULL)
        return SIGNATURE_MISSING;

    char b64[SIG_MAX_B64];
    size_t n = fread(b64, 1, sizeof(b64) - 1, fp);
    fclose(fp);
    b64[n] = '\0';
    while (n > 0 && (b64[n - 1] == '\n' || b64[n - 1] == '\r' ||
                     b64[n - 1] == ' ' || b64[n - 1] == '\t'))
        b64[--n] = '\0';

    return verify_b64(certpath, msg, msglen, b64, errstr);
}

/*
 * Verify the embedded signature block in sector `sector` of the open ISO file
 * descriptor isofd over msg[0..msglen) using the certificate at certpath. If
 * the sector does not start with the magic header it is treated as "no
 * signature present" and SIGNATURE_MISSING is returned. See signature.h.
 */
int signature_verify_block(const char *const certpath, const unsigned char *const msg,
                           const size_t msglen, const int isofd, const off_t sector,
                           char **errstr) {
    char block[SIG_BLOCK_SIZE + 1];
    const off_t off = sector * (off_t) SECTOR_SIZE;
    if (lseek(isofd, off, SEEK_SET) < 0)
        return SIGNATURE_MISSING;
    ssize_t r = read(isofd, block, SIG_BLOCK_SIZE);
    if (r < (ssize_t) strlen(SIG_BLOCK_MAGIC))
        return SIGNATURE_MISSING;
    block[r] = '\0';

    const size_t magiclen = strlen(SIG_BLOCK_MAGIC);
    if (memcmp(block, SIG_BLOCK_MAGIC, magiclen) != 0)
        return SIGNATURE_MISSING;

    /* The base64 payload is the next line after the magic, up to the newline
     * or NUL padding. */
    char *b64 = block + magiclen;
    for (char *p = b64; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
    }
    if (b64[0] == '\0')
        return SIGNATURE_MISSING;

    return verify_b64(certpath, msg, msglen, b64, errstr);
}

/*
 * Validate the trust chain of the certificate at certpath against the trusted
 * CA certificate(s) given by capath (a PEM bundle) and/or cadir (a c_rehash
 * directory) using X509_verify_cert().
 *
 * The CA locations are loaded into an X509_STORE as trust anchors; the
 * end-entity certificate is then verified against that store. A successful
 * verification means certpath chains to one of the supplied CAs and all
 * certificates in the chain are within their validity period. Revocation
 * (CRL/OCSP) is NOT checked.
 *
 * At least one of capath/cadir must be non-NULL. These map directly onto the
 * two arguments of X509_STORE_load_locations(), i.e. "openssl verify -CAfile"
 * (capath) and "-CApath" (cadir).
 *
 * Returns SIGNATURE_VALID (trusted), SIGNATURE_UNTRUSTED (does not chain / is
 * expired), or SIGNATURE_ERROR (I/O or internal error; sets *errstr).
 */
int signature_verify_chain(const char *const certpath, const char *const capath,
                           const char *const cadir, char **errstr) {
    int result = SIGNATURE_ERROR;

    if ((capath == NULL || capath[0] == '\0') && (cadir == NULL || cadir[0] == '\0')) {
        if (errstr)
            *errstr = "No CA file or CA directory supplied.";
        return SIGNATURE_ERROR;
    }

    X509 *cert = load_certificate(certpath);
    if (cert == NULL) {
        if (errstr)
            *errstr = "Unable to read certificate.";
        return SIGNATURE_ERROR;
    }

    X509_STORE *store = X509_STORE_new();
    if (store == NULL) {
        if (errstr)
            *errstr = "Out of memory creating trust store.";
        X509_free(cert);
        return SIGNATURE_ERROR;
    }

    /* Load the trusted CA certificate(s). X509_STORE_load_locations accepts a
     * PEM file (first arg, may hold one or more concatenated CAs) and/or a
     * hashed CA directory (second arg, c_rehash layout). Either may be NULL. */
    const char *const file = (capath != NULL && capath[0] != '\0') ? capath : NULL;
    const char *const dir = (cadir != NULL && cadir[0] != '\0') ? cadir : NULL;
    if (X509_STORE_load_locations(store, file, dir) != 1) {
        if (errstr)
            *errstr = "Unable to read CA certificate file or directory.";
        X509_STORE_free(store);
        X509_free(cert);
        return SIGNATURE_ERROR;
    }

    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    if (ctx == NULL) {
        if (errstr)
            *errstr = "Out of memory creating verify context.";
        X509_STORE_free(store);
        X509_free(cert);
        return SIGNATURE_ERROR;
    }

    if (X509_STORE_CTX_init(ctx, store, cert, NULL) != 1) {
        if (errstr)
            *errstr = "Unable to initialize certificate verification.";
        X509_STORE_CTX_free(ctx);
        X509_STORE_free(store);
        X509_free(cert);
        return SIGNATURE_ERROR;
    }

    const int v = X509_verify_cert(ctx);
    if (v == 1) {
        result = SIGNATURE_VALID;
    } else {
        /* v == 0 means the chain could not be verified (untrusted/expired);
         * the specific reason is available for diagnostics. */
        if (errstr)
            *errstr = (char *) X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx));
        result = SIGNATURE_UNTRUSTED;
    }

    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(cert);
    return result;
}
