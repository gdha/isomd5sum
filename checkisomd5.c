/*
 * checkisomd5 - simple program to check implanted md5sum
 * Copyright (C) 2001-2013 Red Hat, Inc.
 * Michael Fulbright <msf@redhat.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <popt.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "md5.h"
#include "libcheckisomd5.h"

struct progressCBData {
    int verbose;
    int gauge;
    int gaugeat;
};

static int user_bailing_out(void) {
    struct timeval timev;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(0, &rfds);

    timev.tv_sec = 0;
    timev.tv_usec = 0;

    if (select(1, &rfds, NULL, NULL, &timev) && getchar() == 27)
        return 1;

    return 0;
}

static int outputCB(void *const co, const long long offset, const long long total) {
    struct progressCBData *const data = co;
    int gaugeval = -1;

    if (data->verbose) {
        printf("\rChecking: %05.1f%%", (100.0 * (double) offset) / (double) total);
        fflush(stdout);
    }
    if (data->gauge) {
        gaugeval = (int) ((100.0 * (double) offset) / (double) total);
        if (gaugeval != data->gaugeat) {
            printf("%d\n", gaugeval);
            fflush(stdout);
            data->gaugeat = gaugeval;
        }
    }
    return user_bailing_out();
}

static int usage(void) {
    fprintf(stderr, "Usage: checkisomd5 [--md5sumonly] [--verbose] [--gauge] [--cert <certfile> [--ca <cafile>] [--CApath <cadir>]] <isofilename>|<blockdevice>\n\n");
    return 1;
}

/* Process the result code and return the proper exit status value
 *
 * return 1 for failures, 0 for good checksum and 2 if aborted.
 */
static int processExitStatus(const int rc) {
    char *result;
    int exit_rc;

    switch (rc) {
        case ISOMD5SUM_CHECK_FAILED:
            result = "FAIL.\n\nIt is not recommended to use this media.";
            exit_rc = 1;
            break;
        case ISOMD5SUM_CHECK_ABORTED:
            result = "UNKNOWN.\n\nThe media check was aborted.";
            exit_rc = 2;
            break;
        case ISOMD5SUM_CHECK_NOT_FOUND:
            result = "NA.\n\nNo checksum information available, unable to verify media.";
            exit_rc = 1;
            break;
        case ISOMD5SUM_FILE_NOT_FOUND:
            result = "NA.\n\nFile not found.";
            exit_rc = 1;
            break;
        case ISOMD5SUM_CHECK_PASSED:
            result = "PASS.\n\nIt is OK to use this media.";
            exit_rc = 0;
            break;
        default:
            result = "checkisomd5 ERROR - bad return value";
            exit_rc = 1;
            break;
    }

    fprintf(stderr, "\nThe media check is complete, the result is: %s\n", result);

    return exit_rc;
}

int main(int argc, const char **argv) {
    struct progressCBData data;
    memset(&data, 0, sizeof(data));
    data.verbose = 0;
    data.gauge = 0;

    int md5only = 0;
    int help = 0;
    char *cert = NULL;
    char *ca = NULL;
    char *cadir = NULL;

    struct poptOption options[] = {
        { "md5sumonly", 'o', POPT_ARG_NONE, &md5only, 0 },
        { "verbose", 'v', POPT_ARG_NONE, &data.verbose, 0 },
        { "gauge", 'g', POPT_ARG_NONE, &data.gauge, 0 },
        { "cert", 'c', POPT_ARG_STRING, &cert, 0 },
        { "ca", 'a', POPT_ARG_STRING, &ca, 0 },
        { "CApath", 'A', POPT_ARG_STRING, &cadir, 0 },
        { "help", 'h', POPT_ARG_NONE, &help, 0 },
        { 0, 0, 0, 0, 0 }
    };

    poptContext optCon = poptGetContext("checkisomd5", argc, argv, options, 0);

    int rc = poptGetNextOpt(optCon);
    if (rc < -1) {
        fprintf(stderr, "bad option %s: %s\n",
                poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
                poptStrerror(rc));
        poptFreeContext(optCon);
        return 1;
    }

    if (help) {
        poptFreeContext(optCon);
        return usage();
    }

    const char **args = poptGetArgs(optCon);
    if (!args || !args[0] || !args[0][0]) {
        poptFreeContext(optCon);
        return usage();
    }

    if (md5only | data.verbose) {
        rc = printMD5SUM(args[0]);
        if (rc < 0) {
            poptFreeContext(optCon);
            return processExitStatus(rc);
        }
    }

    if (md5only) {
        poptFreeContext(optCon);
        return 0;
    }

    printf("Press [Esc] to abort check.\n");

    /* Only change the terminal settings if stdin is a terminal. */
    static struct termios oldt;
    const int tty = isatty(0) && tcgetattr(0, &oldt) == 0;
    if (tty) {
        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO | ECHONL | ISIG | IEXTEN);
        tcsetattr(0, TCSANOW, &newt);
    }
    rc = mediaCheckFile(args[0], outputCB, &data);
    if (tty)
        tcsetattr(0, TCSANOW, &oldt);

    if (data.verbose)
        printf("\n");

    int exit_rc = processExitStatus(rc);

    /*
     * Optional signature check (--cert). Runs AFTER the md5 media check and
     * reports a separate "Signature status:" line. If --ca and/or --CApath is
     * also supplied, the certificate's trust chain is validated
     * (X509_verify_cert) BEFORE its key is trusted, so a signature made with an
     * untrusted certificate is rejected. --ca names a PEM CA bundle (like
     * "openssl verify -CAfile"); --CApath names a c_rehash directory of CA
     * certificates (like "-CApath"); either or both may be given. Any outcome
     * other than VALID (invalid/missing/no-md5/untrusted/error) is treated as a
     * failure and forces a non-zero exit, but only if the md5 check itself
     * passed - a prior md5 failure already owns the exit code.
     */
    if (cert != NULL && cert[0] != '\0') {
        const char *capath = (ca != NULL && ca[0] != '\0') ? ca : NULL;
        const char *cap_dir = (cadir != NULL && cadir[0] != '\0') ? cadir : NULL;
        const int have_ca = capath != NULL || cap_dir != NULL;
        int sig = verifyISOCertChain(args[0], cert, capath, cap_dir);
        switch (sig) {
            case ISOMD5SUM_SIG_VALID:
                if (have_ca)
                    fprintf(stderr, "Signature status: VALID (md5sum signed by the supplied certificate, which chains to the supplied CA).\n");
                else
                    fprintf(stderr, "Signature status: VALID (md5sum signed by the supplied certificate).\n");
                break;
            case ISOMD5SUM_SIG_INVALID:
                fprintf(stderr, "Signature status: INVALID (signature does not match the certificate).\n");
                if (exit_rc == 0)
                    exit_rc = 1;
                break;
            case ISOMD5SUM_SIG_UNTRUSTED:
                fprintf(stderr, "Signature status: UNTRUSTED (the certificate does not chain to the supplied CA).\n");
                if (exit_rc == 0)
                    exit_rc = 1;
                break;
            case ISOMD5SUM_SIG_MISSING:
                fprintf(stderr, "Signature status: MISSING (no embedded signature found in the ISO).\n");
                if (exit_rc == 0)
                    exit_rc = 1;
                break;
            case ISOMD5SUM_SIG_NO_MD5:
                fprintf(stderr, "Signature status: NA (no embedded md5sum to verify).\n");
                if (exit_rc == 0)
                    exit_rc = 1;
                break;
            default:
                fprintf(stderr, "Signature status: ERROR (could not verify signature; "
                                "the certificate may not match the signing key type, "
                                "or the CA file/directory could not be read).\n");
                if (exit_rc == 0)
                    exit_rc = 1;
                break;
        }
    } else if ((ca != NULL && ca[0] != '\0') || (cadir != NULL && cadir[0] != '\0')) {
        fprintf(stderr, "Signature status: NA (--ca/--CApath requires --cert).\n");
        if (exit_rc == 0)
            exit_rc = 1;
    }

    poptFreeContext(optCon);
    return exit_rc;
}
