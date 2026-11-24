# Integrating the signed-ISO scripts into ReaR

This directory stages the changes that make ReaR (Relax-and-Recover) use the
signature support of this `isomd5sum` fork. The files mirror the layout of
`/usr/share/rear/`, so each one has a fixed destination.

| Staged file | Destination in ReaR | Action |
|---|---|---|
| `default.conf.snippet` | `/usr/share/rear/conf/default.conf` | Paste the contents after the `ISO_PREFIX=` line (about line 789 in ReaR 2.6) |
| `output/ISO/Linux-i386/840_implantisomd5.sh` | `/usr/share/rear/output/ISO/Linux-i386/840_implantisomd5.sh` | Replace the existing file |
| `skel/default/etc/scripts/system-setup.d/70-checkisomd5.sh` | `/usr/share/rear/skel/default/etc/scripts/system-setup.d/70-checkisomd5.sh` | Replace the existing file |

Everything was checked against ReaR 2.6 (2020-06-17). Other versions may have
different line numbers or stage layouts.

## 1. What the integration does

ReaR already calls `implantisomd5` after it builds the ISO, and runs
`checkisomd5` when the rescue system boots. That only protects against
corruption, because anyone can recompute an md5sum.

This fork's `implantisomd5` can also sign the md5sum with a private key and
store the signature inside the ISO, in the trailing skip-sectors region. There
is no detached `.sig` file. `checkisomd5 --cert` verifies that signature with
the matching X.509 certificate. With `--ca` or `--CApath` it also checks that
the certificate chains to a trusted CA.

The flow is:

```
rear mkrescue / mkbackup
  └─ output stage: 840_implantisomd5.sh
       implantisomd5 --cert CERT --sign-key KEY rear-HOST.iso
                                  (signature is embedded in the ISO)

Rescue system boot (from CD/ISO/virtual media)
  └─ system-setup.d/70-checkisomd5.sh
       checkisomd5 --verbose --cert CERT [--ca CA] [--CApath DIR] /dev/sr0
```

The private key stays on the host that runs `rear`. Only the certificate (and
optionally the CA material) is shipped into the rescue image.

## 2. Prerequisites

1. Build and install this fork of `isomd5sum` on the host that runs ReaR. The
   build steps are in `../README`, `../RPM-BUILD.md` and `../DEB-BUILD.md`.
   Check that the installed tools know the new options:
   ```
   implantisomd5 2>&1 | grep -- --sign-key
   checkisomd5   2>&1 | grep -- --cert
   ```
   ReaR resolves the tools with `type -p`, so the patched binaries must come
   first in `PATH`. A stock `isomd5sum` package does not know `--cert`, so
   `implantisomd5` would fail when signing is enabled.
2. Create a signing key and certificate. A self-signed pair is enough for a
   test:
   ```
   openssl req -x509 -newkey rsa:4096 -nodes -days 3650 \
       -subj "/CN=rear-iso-signing" \
       -keyout /etc/pki/tls/private/cert.key -out /etc/pki/tls/certs/cert.pem
   chmod 600 /etc/pki/tls/private/cert.key
   ```
   For production, use a certificate issued by your own CA and also provide
   the CA bundle (`ISO_SIGNING_CA_FILE`) or hashed directory
   (`ISO_SIGNING_CA_DIR`).
3. ReaR must be run with `OUTPUT=ISO`. Other output methods do not run
   `840_implantisomd5.sh`.

## 3. Step-by-step integration

Back up the originals first:

```
cd /usr/share/rear
cp -a conf/default.conf conf/default.conf.orig
cp -a output/ISO/Linux-i386/840_implantisomd5.sh output/ISO/Linux-i386/840_implantisomd5.sh.orig
cp -a skel/default/etc/scripts/system-setup.d/70-checkisomd5.sh \
      skel/default/etc/scripts/system-setup.d/70-checkisomd5.sh.orig
```

Do not leave `*.orig` files in the ReaR tree: ReaR sources every `*.sh`
file in a stage directory, and a stray copy may confuse you or packaging
tools. Move them elsewhere once you are done.

### 3.1 Add the defaults to `default.conf`

Paste `default.conf.snippet` after `ISO_PREFIX="rear-$HOSTNAME"`. It defines
four variables, all empty by default:

| Variable | Meaning |
|---|---|
| `ISO_SIGNING_CERT` | X.509 certificate (PEM). Its serial is recorded in the ISO and its public key verifies the signature. |
| `ISO_SIGNING_KEY` | Matching private key (PEM). Used only when the ISO is built. |
| `ISO_SIGNING_CA_FILE` | Optional CA bundle for chain validation (`openssl verify -CAfile` style). |
| `ISO_SIGNING_CA_DIR` | Optional `c_rehash` directory of CA certificates (`-CApath` style). |

ReaR keeps all user-settable variables with their defaults in
`default.conf`. The rescue system also sources `default.conf` first, then
`site.conf`, `local.conf` and `rescue.conf` (see
`skel/default/etc/scripts/system-setup`), so the variables exist at recovery
time and take the user's values.

With both `ISO_SIGNING_CERT` and `ISO_SIGNING_KEY` empty, behaviour is
identical to stock ReaR: the md5sum is implanted without a signature.

### 3.2 Replace `840_implantisomd5.sh`

What the staged version does:

1. Exits quietly if `implantisomd5` is not installed (same as the original).
2. Builds the `implant_opts` array. Signing is enabled only when both
   `ISO_SIGNING_KEY` and `ISO_SIGNING_CERT` are set and readable. If they are
   set but unreadable, it prints an error with `LogPrintError` and implants
   the plain md5sum, so the ISO is still produced.
3. Adds the certificate and the optional CA file or directory to
   `COPY_AS_IS`, and `checkisomd5` to `PROGS` (see section 4, this does not
   work as intended in the output stage).
4. Runs `implantisomd5 "${implant_opts[@]}"` on the single ISO
   (`$ISO_PREFIX.iso`) and on every split ISO (`${ISO_PREFIX}_*.iso`).

### 3.3 Replace `70-checkisomd5.sh`

This script is sourced by `system-setup` while the rescue system boots. The
staged version:

1. Returns if there is no CD-ROM drive or no readable medium.
2. Returns if `checkisomd5` is not in the rescue image.
3. Always uses `--verbose`. If `ISO_SIGNING_CERT` is set and the file exists
   in the rescue system, it adds `--cert`, plus `--ca` and `--CApath` when
   those files or directories exist.
4. Runs `checkisomd5` on `/dev/$cdrom`. It reports the md5 result and a
   separate `Signature status:` line.

### 3.4 Configure the site

In `/etc/rear/local.conf` (or `site.conf`):

```
OUTPUT=ISO
ISO_SIGNING_CERT="/etc/pki/tls/certs/cert.pem"
ISO_SIGNING_KEY="/etc/pki/tls/private/cert.key"
# optional chain validation at recovery time
#ISO_SIGNING_CA_FILE="/etc/pki/tls/certs/ca-bundle.pem"
#ISO_SIGNING_CA_DIR="/etc/pki/tls/certs"
```

`local.conf` is copied into the rescue image as `/etc/rear/local.conf`, so
the same paths are visible at boot. Do not put the private key path anywhere
that causes the key itself to be copied into the image. `ISO_SIGNING_KEY` is
only read on the build host. Also check that `COPY_AS_IS`/`COPY_AS_IS_EXCLUDE`
do not pull in `/etc/pki/tls/private`.

## 4. Known problem: stage ordering of `COPY_AS_IS` and `PROGS`

`rear mkrescue` runs the stages in this order (`lib/mkrescue-workflow.sh`):

```
prep -> layout/save -> rescue -> build -> pack -> output
```

The rescue initramfs is assembled in `build` (which reads `COPY_AS_IS` and
`PROGS`) and compressed in `pack`. `840_implantisomd5.sh` runs in `output`,
after the initramfs already exists. Changes it makes to `PROGS` and
`COPY_AS_IS` therefore have no effect on the image. The original ReaR script
has the same issue with `PROGS+=( checkisomd5 )`: it only works if
`checkisomd5` is already included some other way (for example through
`REQUIRED_PROGS`/`PROGS` in `local.conf`, or because it was pulled in as a
library dependency).

Consequence: with the staged files as they are, the ISO is signed, but the
certificate may be missing in the rescue system. In that case
`70-checkisomd5.sh` silently skips the signature check, because it tests
`-r "$ISO_SIGNING_CERT"`. A skipped check looks like a pass, which is not
acceptable for a security feature.

Recommended fix, to be applied during integration:

1. Add a new script in the `prep` stage, for example
   `/usr/share/rear/prep/ISO/Linux-i386/335_prepare_isomd5sum_signing.sh`
   (any number after `330_find_isolinux.sh` works). Move there:
   - `PROGS+=( checkisomd5 )` when `implantisomd5` exists,
   - the readability checks for `ISO_SIGNING_KEY`/`ISO_SIGNING_CERT`,
   - the `COPY_AS_IS+=( ... )` lines for the certificate and CA material.
   Use `Error` instead of a warning if signing was requested but a file is
   unreadable.
2. Keep in `840_implantisomd5.sh` only the call to `implantisomd5`. It can
   rely on a variable set by the prep script, for example
   `ISO_SIGNING_ENABLED=yes`, or repeat the same readability test.
3. In `70-checkisomd5.sh`, when `ISO_SIGNING_CERT` is set but the file is
   missing, print an error instead of skipping. For example:
   ```
   if [[ -n "$ISO_SIGNING_CERT" && ! -r "$ISO_SIGNING_CERT" ]] ; then
       echo "ERROR: signing certificate $ISO_SIGNING_CERT missing in rescue system"
   fi
   ```
4. Add the new file to the staging tree and to the table at the top of this
   README.

Until this is done, work around it in `local.conf`:

```
PROGS+=( checkisomd5 )
COPY_AS_IS+=( "$ISO_SIGNING_CERT" )
```

## 5. Behaviour at recovery time

`checkisomd5` runs the md5 media check first, then the signature check. The
signature result is a separate line. Typical results:

| Situation | Result |
|---|---|
| ISO intact and signed by the configured certificate | md5 `PASS`, signature valid |
| ISO signed, but `--cert` does not match the signer | signature check fails |
| ISO built without signing | `Signature status: MISSING` |
| `--ca`/`--CApath` given and the certificate does not chain to it | certificate rejected |
| `--ca`/`--CApath` given without `--cert` | `Signature status: NA` |

Revocation (CRL/OCSP) is not checked. See `../README` for the exact status
wording and the details of the trust model.

The check in `70-checkisomd5.sh` only prints its result. It does not stop the
boot. If you need recovery to halt on a bad signature, extend the script to
act on the `checkisomd5` exit status. Check in `checkisomd5.c` which exit
code is used for a signature failure before depending on it.

## 6. Verification

1. Build the ISO and look for the log line
   `Embedding md5sum signature into the ISO image(s) using certificate ...`:
   ```
   rear -v mkrescue
   ```
2. Verify on the build host:
   ```
   checkisomd5 --verbose --cert /etc/pki/tls/certs/cert.pem /var/lib/rear/output/rear-$(hostname -s).iso
   ```
   Adjust the path to your `ISO_DIR` and `ISO_PREFIX`.
3. Negative test: verify with a different certificate, and with an ISO built
   without signing. Both must report a failed or missing signature.
4. Boot the ISO in a VM, attached as CD-ROM. Watch for
   `**** Checking the md5sum and embedded signature of the cdrom or iso image ****`
   during startup. If you see the shorter `**** Checking the md5sum of the
   cdrom or iso image ****`, the certificate is not in the rescue image (see
   section 4).
5. Check the rescue image contents:
   ```
   lsinitrd /var/lib/rear/output/... # or unpack the initramfs
   ls /etc/pki/tls/certs/cert.pem
   type checkisomd5
   ```

## 7. Security notes

- Keep `ISO_SIGNING_KEY` root-only (mode 600). It is never copied into the
  rescue image by these scripts.
- Without `ISO_SIGNING_CA_FILE`/`ISO_SIGNING_CA_DIR`, the check trusts
  whatever certificate is shipped in the image. An attacker who can modify the
  whole ISO can replace both the certificate and the signature. The signature
  then protects against accidental corruption and tampering with the ISO only
  when the certificate comes from a trusted separate source, such as a CA
  chain or a certificate you compare out of band.
- Implanting modifies the ISO in place. Sign as the last step, after anything
  else that writes to the ISO (for example copying or `ISO_FILES` changes).
  Later processing that rewrites the file invalidates the signature.

## 8. Upstreaming to ReaR

For a pull request to `rear/rear`:

1. Put the new variables in `usr/share/rear/conf/default.conf` with the
   comments from the snippet.
2. Add the prep script from section 4 so the rescue image is complete.
3. Keep the behaviour unchanged when the variables are empty, and document
   them in `doc/user-guide`.
4. Mention the minimum `isomd5sum` version that supports `--cert` and
   `--sign-key`, and keep a fallback for stock `isomd5sum` (the scripts should
   not pass `--cert` unless the installed tool supports it, for example by
   testing `implantisomd5 --help` output).

## 9. Rollback

Restore the three `.orig` files (or reinstall the ReaR package, which
overwrites them) and remove the `ISO_SIGNING_*` lines from `local.conf`.
Previously created ISOs keep working: a signed ISO is still a valid ISO with
a valid md5sum, and stock `checkisomd5` ignores the signature.
