/*
 * Copyright (C) 2001-2013 Red Hat, Inc.
 *
 * Michael Fulbright <msf@redhat.com>
 * Dustin Kirkland  <dustin.dirkland@gmail.com>
 *      Added support for checkpoint fragment sums;
 *      Exits media check as soon as bad fragment md5sum'ed
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
 * libimplantisomd5 - write integrity (and optional certificate) metadata into
 * an ISO9660 image.
 *
 * WHAT THIS DOES
 *   An ISO9660 image has a Primary Volume Descriptor (PVD) that contains a
 *   512-byte "Application Used" area (ECMA-119 8.4.32) which the filesystem
 *   itself ignores. This tool stores its metadata there, so an image can be
 *   verified later given nothing but the image (plus, for signatures, a
 *   detached .sig file and a certificate).
 *
 * THE RECORD FORMAT written into the 512-byte area is a sequence of
 * semicolon-terminated "KEY = VALUE" fields, padded with spaces:
 *
 *   ISO MD5SUM = <32 hex>;            md5 of the whole image (see note below)
 *   SKIPSECTORS = <n>;                trailing sectors excluded from the md5
 *   RHLISOSTATUS=<0|1>;               "supported media" flag
 *   FRAGMENT SUMS = <60 hex>;         per-fragment checkpoint sums
 *   FRAGMENT COUNT = <n>;             number of fragments
 *   CERTIFICATE SERIAL = <hex>;       OPTIONAL - X.509 serial (see certificate.c)
 *   THIS IS NOT THE SAME AS RUNNING MD5SUM ON THIS ISO!!   (human reminder)
 *
 *   The md5 is NOT a plain md5sum of the file: the 512-byte area itself and
 *   the trailing SKIPSECTORS sectors are excluded. checkisomd5 reproduces the
 *   exact same exclusions, which is why embedding metadata here does not break
 *   verification. The parser that reads these fields back lives in
 *   utilities.c (parsepvd).
 *
 * OPTIONAL CERTIFICATE SERIAL (--cert / "cert" argument)
 *   Records WHICH certificate an image is associated with. This is only a
 *   label; it is not a cryptographic proof. See certificate.c.
 *
 * OPTIONAL SIGNATURE (--sign-key / "signkey" argument)
 *   Cryptographically signs the md5 hex string with a private key and writes a
 *   detached base64 signature next to the image as "<iso>.sig". The signature
 *   is kept out of the PVD area on purpose: an RSA-2048 signature (512 hex
 *   chars) would not fit in the ~220 bytes of free space. checkisomd5 --cert
 *   verifies it using the certificate's public key. See signature.c.
 *
 * ORDERING NOTE
 *   The certificate serial is extracted, and the signature is produced, BEFORE
 *   the PVD area is overwritten. That way an unreadable certificate or key
 *   aborts the run without having modified the image.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "md5.h"
#include "libimplantisomd5.h"
#include "utilities.h"
#include "certificate.h"
#include "signature.h"

/*
 * Append valstr to the in-memory appdata buffer at position *loc and advance
 * *loc. Guards against overrunning the fixed 512-byte area; the fields are
 * built up in memory first and written to the image in one go at the end.
 */
static int writeAppData(unsigned char *const appdata, const char *const valstr, size_t *loc, char **errstr) {
    size_t vallen = strlen(valstr);
    if (*loc + vallen >= APPDATA_SIZE) {
        *errstr = "Attempted to write too much appdata.";
        return -1;
    }

    memcpy(appdata + *loc, valstr, vallen);

    *loc += vallen;
    return 0;
}

/*
 * Public entry point: open the ISO by path and implant metadata.
 *   iso       - path to the image, opened read/write.
 *   cert      - optional path to an X.509 certificate; its serial is embedded.
 *   signkey   - optional path to a PEM private key; if set, the md5 is signed
 *               and the signature is written to "<iso>.sig".
 *   supported - value for the RHLISOSTATUS flag.
 *   forceit   - overwrite an application-data area that is already in use.
 *   quiet     - suppress progress/diagnostic output when non-zero.
 *   errstr    - set to a (possibly printf-format) message on failure.
 * Returns 0 on success, negative on failure.
 */
int implantISOFile(const char *iso, const char *cert, const char *signkey, int supported, int forceit, int quiet, char **errstr) {
    int isofd = open(iso, O_RDWR | O_BINARY);
    if (isofd < 0) {
        *errstr = "Error - Unable to open file %s";
        return -1;
    }

    /* The detached signature, if requested, is written next to the ISO as
     * "<iso>.sig". */
    char *sigpath = NULL;
    if (signkey != NULL && signkey[0] != '\0') {
        const size_t len = strlen(iso) + 4 + 1; /* ".sig" + NUL */
        sigpath = malloc(len);
        if (sigpath == NULL) {
            *errstr = "Out of memory.";
            close(isofd);
            return -1;
        }
        snprintf(sigpath, len, "%s.sig", iso);
    }

    int rc = implantISOFD(isofd, cert, signkey, sigpath, supported, forceit, quiet, errstr);
    close(isofd);
    free(sigpath);
    return rc;
}

/*
 * Core implant routine working on an already-open file descriptor.
 * Takes sigpath (the precomputed "<iso>.sig" path) separately because an fd
 * has no associated filename. All other parameters are as implantISOFile().
 */
int implantISOFD(int isofd, const char *cert, const char *signkey, const char *sigpath, int supported, int forceit, int quiet, char **errstr) {

    /* If a certificate was supplied, extract its serial number up front so we
     * fail before touching the ISO when the certificate is unreadable. */
    char certserial[CERT_SERIAL_SIZE];
    certserial[0] = '\0';
    if (cert != NULL && cert[0] != '\0') {
        if (certificate_serial(cert, certserial, errstr) != 0)
            return -1;
    }

    off_t pvd_offset;
    const off_t isosize = primary_volume_size(isofd, &pvd_offset);
    if (isosize == 0) {
        *errstr = "Could not find primary volume!";
        return -1;
    }

    lseek(isofd, pvd_offset + APPDATA_OFFSET, SEEK_SET);
    unsigned char appdata[APPDATA_SIZE];
    if (read(isofd, appdata, APPDATA_SIZE) <= 0) {
        *errstr = "Failed to read application data from file.";
        return -errno;
    }

    if (!forceit) {
        for (size_t i = 0; i < APPDATA_SIZE; i++) {
            if (appdata[i] != ' ') {
                if (!quiet) {
                    /* Build a printable, NUL-terminated copy of the current
                     * application data, trimming trailing blanks, so the user
                     * can see what already occupies the area. */
                    char current[APPDATA_SIZE + 1];
                    size_t len = APPDATA_SIZE;
                    while (len > 0 && (appdata[len - 1] == ' ' || appdata[len - 1] == '\0'))
                        len--;
                    for (size_t k = 0; k < len; k++) {
                        unsigned char c = appdata[k];
                        current[k] = (c >= 0x20 && c < 0x7f) ? (char) c : '.';
                    }
                    current[len] = '\0';

                    fprintf(stderr,
                            "The ISO9660 application data area (PVD offset %lld, %d bytes) "
                            "already contains data.\n"
                            "First non-blank byte is at offset %zu within the area.\n"
                            "Current content: \"%s\"\n"
                            "This is often an application identifier written by the ISO "
                            "authoring tool (e.g. genisoimage/xorrisofs/mkisofs).\n"
                            "Re-run with --force to overwrite it and implant the md5sum.\n",
                            (long long)(pvd_offset + APPDATA_OFFSET), APPDATA_SIZE, i, current);
                }
                *errstr = "Application data has been used - not implanting md5sum! (use --force to overwrite)";
                return -1;
            }
        }
    } else {
        /* Write out blanks to erase old app data. */
        lseek(isofd, pvd_offset + APPDATA_OFFSET, SEEK_SET);
        memset(appdata, ' ', APPDATA_SIZE);
        ssize_t error = write(isofd, appdata, APPDATA_SIZE);
        if (error < 0) {
            *errstr = "Write failed.";
            return error;
        }
    }

    /* Rewind, compute md5sum. */
    lseek(isofd, 0LL, SEEK_SET);

    MD5_CTX hashctx;
    MD5_Init(&hashctx);
    char fragmentsums[FRAGMENT_SUM_SIZE + 1];
    *fragmentsums = '\0';

    const size_t pagesize = (size_t) getpagesize();
    const size_t buffer_size = NUM_SYSTEM_SECTORS * SECTOR_SIZE;
    unsigned char *buffer;
    buffer = aligned_alloc(pagesize, buffer_size * sizeof(*buffer));

    const off_t total_size = isosize - SKIPSECTORS * SECTOR_SIZE;
    const off_t fragment_size = total_size / (FRAGMENT_COUNT + 1);
    size_t previous_fragment = 0UL;
    off_t offset = 0LL;
    while (offset < total_size) {
        const size_t nbyte = MIN((size_t)(total_size - offset), buffer_size);
        ssize_t nread = read(isofd, buffer, nbyte);
        if (nread <= 0L)
            break;

        MD5_Update(&hashctx, buffer, (unsigned int) nread);
        const size_t current_fragment = offset / fragment_size;
        const size_t fragmentsize = FRAGMENT_SUM_SIZE / FRAGMENT_COUNT;
        /* If we're onto the next fragment, calculate the previous sum and check. */
        if (current_fragment != previous_fragment) {
            validate_fragment(&hashctx, current_fragment, fragmentsize, NULL, fragmentsums);
            previous_fragment = current_fragment;
        }

        offset += nread;
    }
    free(buffer);

    char hashsum[HASH_SIZE + 1];
    md5sum(hashsum, &hashctx);

    /* If a signing key was supplied, sign the md5 hex string and write a
     * detached signature file. Do this before modifying the ISO so a signing
     * failure aborts cleanly. */
    if (signkey != NULL && signkey[0] != '\0') {
        if (sigpath == NULL) {
            *errstr = "Internal error: no signature path.";
            return -1;
        }
        if (signature_sign_file(signkey, (const unsigned char *) hashsum,
                                 strlen(hashsum), sigpath, errstr) != 0)
            return -1;
        if (!quiet)
            printf("signature written to %s\n", sigpath);
    }

    if (!quiet) {
        printf("Inserting md5sum into iso image...\n");
        printf("md5 = %s\n", hashsum);
        printf("Inserting fragment md5sums into iso image...\n");
        printf("fragmd5 = %s\n", fragmentsums);
        printf("frags = %lu\n", FRAGMENT_COUNT);
    }
    memset(appdata, ' ', APPDATA_SIZE);

    size_t loc = 0;
    if (writeAppData(appdata, "ISO MD5SUM = ", &loc, errstr))
        return -1;
    if (writeAppData(appdata, hashsum, &loc, errstr))
        return -1;
    if (writeAppData(appdata, ";", &loc, errstr))
        return -1;

    char appdata_buffer[APPDATA_SIZE];
    snprintf(appdata_buffer, APPDATA_SIZE, "SKIPSECTORS = %lld", SKIPSECTORS);

    if (writeAppData(appdata, appdata_buffer, &loc, errstr))
        return -1;
    if (writeAppData(appdata, ";", &loc, errstr))
        return -1;

    if (!quiet)
        printf("Setting supported flag to %d\n", supported);
    static const char status[] = "RHLISOSTATUS=%d";
    char tmp[sizeof(status) / sizeof(*status)];
    snprintf(tmp, sizeof(status) / sizeof(*status), status, supported);
    if (writeAppData(appdata, tmp, &loc, errstr))
        return -1;

    if (writeAppData(appdata, ";", &loc, errstr))
        return -1;

    if (writeAppData(appdata, "FRAGMENT SUMS = ", &loc, errstr))
        return -1;
    if (writeAppData(appdata, fragmentsums, &loc, errstr))
        return -1;
    if (writeAppData(appdata, ";", &loc, errstr))
        return -1;

    snprintf(appdata_buffer, APPDATA_SIZE, "FRAGMENT COUNT = %lu", FRAGMENT_COUNT);
    if (writeAppData(appdata, appdata_buffer, &loc, errstr))
        return -1;
    if (writeAppData(appdata, ";", &loc, errstr))
        return -1;

    if (certserial[0] != '\0') {
        if (!quiet)
            printf("certificate serial = %s\n", certserial);
        if (writeAppData(appdata, "CERTIFICATE SERIAL = ", &loc, errstr))
            return -1;
        if (writeAppData(appdata, certserial, &loc, errstr))
            return -1;
        if (writeAppData(appdata, ";", &loc, errstr))
            return -1;
    }

    if (writeAppData(
            appdata, "THIS IS NOT THE SAME AS RUNNING MD5SUM ON THIS ISO!!", &loc, errstr))
        return -1;

    if (lseek(isofd, pvd_offset + APPDATA_OFFSET, SEEK_SET) < 0) {
        *errstr = "Seek failed.";
        return -1;
    }

    ssize_t error = write(isofd, appdata, APPDATA_SIZE);
    if (error < 0) {
        *errstr = "Write failed.";
        return -1;
    }

    errstr = NULL;
    return 0;
}
