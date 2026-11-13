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
    ISOMD5SUM_SIG_VALID = 0,      /* signature verified against the certificate */
    ISOMD5SUM_SIG_INVALID = 1,    /* signature present but does not verify       */
    ISOMD5SUM_SIG_MISSING = 2,    /* no detached signature file (<iso>.sig)      */
    ISOMD5SUM_SIG_NO_MD5 = 3,     /* ISO has no embedded md5sum to verify        */
    ISOMD5SUM_SIG_ERROR = 4,      /* I/O or crypto error                         */
    ISOMD5SUM_SIG_UNTRUSTED = 5   /* cert does not chain to the supplied CA      */
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

/*
 * Like verifyISOCert(), but if capath and/or cadir is non-NULL the certificate's
 * trust chain is first validated against the CA certificate(s) they name (via
 * X509_verify_cert):
 *   capath - a PEM bundle of one or more CA certificates ("-CAfile" style), or NULL.
 *   cadir  - a c_rehash directory of CA certificates ("-CApath" style), or NULL.
 * If the certificate does not chain to a trusted CA (or is expired/not-yet-valid)
 * ISOMD5SUM_SIG_UNTRUSTED is returned and the signature itself is not trusted.
 * If both capath and cadir are NULL this behaves exactly like verifyISOCert().
 */
int verifyISOCertChain(const char *file, const char *certpath, const char *capath,
                       const char *cadir);

#ifdef __cplusplus
}
#endif

#endif
