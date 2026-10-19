#ifndef __LIBCHECKISOMD5_H__
#define __LIBCHECKISOMD5_H__

#ifdef __cplusplus
extern "C" {
#endif

enum isomd5sum_status {
    ISOMD5SUM_FILE_NOT_FOUND = -2,
    ISOMD5SUM_CHECK_NOT_FOUND = -1,
    ISOMD5SUM_CHECK_FAILED = 0,
    ISOMD5SUM_CHECK_PASSED = 1,
    ISOMD5SUM_CHECK_ABORTED = 2
};

/* Result of verifying the detached signature of an ISO against a certificate. */
enum isomd5sum_sig_status {
    ISOMD5SUM_SIG_VALID = 0,     /* signature verified against the certificate */
    ISOMD5SUM_SIG_INVALID = 1,   /* signature present but does not verify       */
    ISOMD5SUM_SIG_MISSING = 2,   /* no detached signature file (<iso>.sig)      */
    ISOMD5SUM_SIG_NO_MD5 = 3,    /* ISO has no embedded md5sum to verify        */
    ISOMD5SUM_SIG_ERROR = 4      /* I/O or crypto error                         */
};

/* For non-zero return value, check is aborted. */
typedef int (*checkCallback)(void *, long long offset, long long total);

int mediaCheckFile(const char *file, checkCallback cb, void *cbdata);
int mediaCheckFD(int isofd, checkCallback cb, void *cbdata);
int printMD5SUM(const char *file);

/*
 * Verify the detached signature "<file>.sig" over the ISO's embedded md5sum
 * using the public key from the X.509 certificate in certpath.
 * Returns one of enum isomd5sum_sig_status.
 */
int verifyISOCert(const char *file, const char *certpath);

#ifdef __cplusplus
}
#endif

#endif
