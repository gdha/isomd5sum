/*
 * signature.h - sign a message with a private key and verify it with the
 * public key from an X.509 certificate.
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
#ifndef ISOMD5_SIGNATURE_H
#define ISOMD5_SIGNATURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result of a signature verification. */
enum signature_status {
    SIGNATURE_VALID = 0,     /* signature verified against the certificate    */
    SIGNATURE_INVALID = 1,   /* signature present but does not verify          */
    SIGNATURE_MISSING = 2,   /* no detached signature file found               */
    SIGNATURE_ERROR = 3,     /* I/O / crypto error (see errstr)                */
    SIGNATURE_UNTRUSTED = 4  /* cert does not chain to the supplied CA         */
};

/*
 * Sign the message (msglen bytes) with the private key in keypath (PEM) and
 * write a base64-encoded detached signature to sigpath.
 *
 * The digest algorithm is SHA-256, except for Ed25519/Ed448 keys which sign
 * the message directly (as required by those algorithms). This works for RSA,
 * EC (ECDSA) and EdDSA keys transparently.
 *
 * Returns 0 on success, -1 on failure (sets *errstr to a static message).
 */
int signature_sign_file(const char *keypath, const unsigned char *msg, size_t msglen,
                         const char *sigpath, char **errstr);

/*
 * Verify the base64 detached signature in sigpath over the message
 * (msglen bytes) using the public key taken from the X.509 certificate in
 * certpath (PEM or DER).
 *
 * Returns one of enum signature_status. On SIGNATURE_ERROR, *errstr is set.
 */
int signature_verify_file(const char *certpath, const unsigned char *msg, size_t msglen,
                          const char *sigpath, char **errstr);

/*
 * Validate the trust chain of the X.509 certificate in certpath against the
 * trusted CA certificate(s) given by capath and/or cadir, using OpenSSL's
 * X509_verify_cert(). This checks that certpath chains up to a trust anchor and
 * that the certificates are within their validity dates. It does NOT consult
 * CRLs/OCSP for revocation.
 *
 *   capath - a PEM file holding one or more concatenated CA certificates
 *            (as with "openssl verify -CAfile"), or NULL.
 *   cadir  - a directory of trusted CA certificates hashed in the OpenSSL
 *            c_rehash layout (as with "openssl verify -CApath"), or NULL.
 *
 * At least one of capath/cadir must be non-NULL; both may be supplied together,
 * in which case the certificate is trusted if it chains to a CA found in either
 * location.
 *
 * Returns SIGNATURE_VALID if the certificate is trusted, SIGNATURE_UNTRUSTED if
 * it does not chain to a supplied CA (or is expired/not-yet-valid), or
 * SIGNATURE_ERROR on an I/O or internal error (sets *errstr).
 */
int signature_verify_chain(const char *certpath, const char *capath, const char *cadir,
                           char **errstr);

#ifdef __cplusplus
}
#endif

#endif /* ISOMD5_SIGNATURE_H */
