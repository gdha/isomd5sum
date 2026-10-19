/*
 * certificate.h - extract an X.509 certificate serial number
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
#ifndef ISOMD5_CERTIFICATE_H
#define ISOMD5_CERTIFICATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length (in characters) of a certificate serial number string that
 * we are willing to store in the ISO application data area. An X.509 serial
 * number is at most 20 octets (RFC 5280 4.1.2.2), i.e. 40 hex characters, but
 * we allow some slack for unusually long serials. */
#define CERT_SERIAL_SIZE 80

/*
 * Read the X.509 certificate in PEM (or DER) format from certpath and store
 * its serial number, formatted as uppercase hexadecimal (exactly as
 * `openssl x509 -noout -serial` prints it, without the "serial=" prefix),
 * into serial. At most CERT_SERIAL_SIZE bytes (including the terminating NUL)
 * are written.
 *
 * Returns 0 on success. On failure returns -1 and, if errstr is non-NULL,
 * sets *errstr to a static description of the error.
 */
int certificate_serial(const char *certpath, char *serial, char **errstr);

#ifdef __cplusplus
}
#endif

#endif /* ISOMD5_CERTIFICATE_H */
