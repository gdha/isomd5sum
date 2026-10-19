/*
 * certificate.c - extract an X.509 certificate serial number via libcrypto
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
 * certificate.c - read an X.509 certificate and return its serial number as a
 * string.
 *
 * PURPOSE
 *   implantisomd5 --cert embeds this serial into the ISO so a reader can tell
 *   WHICH certificate an image is associated with (provenance / labelling).
 *   This is NOT a cryptographic check - the serial is just text and anyone can
 *   rewrite it. Authenticity is provided separately by signature.c.
 *
 * FORMAT
 *   The serial is returned exactly as the command
 *       openssl x509 -in cert.pem -noout -serial
 *   prints it (uppercase hex, no "serial=" prefix), so operators can compare
 *   the two by eye. We achieve byte-for-byte parity by using the very function
 *   the openssl command uses internally, i2a_ASN1_INTEGER() (see below).
 */

#include <stdio.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "certificate.h"

/*
 * Load an X.509 certificate from path. Tries PEM first (the common case for
 * *.pem files), then falls back to DER so either encoding works.
 */
static X509 *load_certificate(const char *const path) {
    BIO *bio = BIO_new_file(path, "rb");
    if (bio == NULL)
        return NULL;

    /* Try PEM. */
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (cert == NULL) {
        /* Rewind and try DER. */
        if (BIO_reset(bio) == 0)
            cert = d2i_X509_bio(bio, NULL);
    }

    BIO_free(bio);
    return cert;
}

int certificate_serial(const char *const certpath, char *const serial, char **errstr) {
    if (certpath == NULL || serial == NULL) {
        if (errstr)
            *errstr = "Invalid certificate arguments.";
        return -1;
    }

    X509 *const cert = load_certificate(certpath);
    if (cert == NULL) {
        if (errstr)
            *errstr = "Unable to read certificate.";
        return -1;
    }

    const ASN1_INTEGER *const asn1_serial = X509_get0_serialNumber(cert);
    if (asn1_serial == NULL) {
        if (errstr)
            *errstr = "Certificate has no serial number.";
        X509_free(cert);
        return -1;
    }

    /*
     * i2a_ASN1_INTEGER writes the serial as uppercase hexadecimal, exactly as
     * the `openssl x509 -serial` command does (it uses the same function).
     * Writing into a memory BIO lets us capture that formatting verbatim.
     */
    BIO *const mem = BIO_new(BIO_s_mem());
    if (mem == NULL) {
        if (errstr)
            *errstr = "Out of memory handling certificate.";
        X509_free(cert);
        return -1;
    }

    if (i2a_ASN1_INTEGER(mem, asn1_serial) <= 0) {
        if (errstr)
            *errstr = "Unable to format certificate serial number.";
        BIO_free(mem);
        X509_free(cert);
        return -1;
    }

    char *data = NULL;
    const long len = BIO_get_mem_data(mem, &data);
    if (len <= 0 || data == NULL || (size_t) len >= CERT_SERIAL_SIZE) {
        if (errstr)
            *errstr = "Certificate serial number is too long.";
        BIO_free(mem);
        X509_free(cert);
        return -1;
    }

    memcpy(serial, data, (size_t) len);
    serial[len] = '\0';

    BIO_free(mem);
    X509_free(cert);
    return 0;
}
