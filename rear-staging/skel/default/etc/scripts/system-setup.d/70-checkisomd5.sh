# if checkisomd5 is available perform a md5 checksum on the CD-ROM or ISO image
# Of course only when there is something mounted on /dev/sr0
#
# When ISO_SIGNING_CERT is set, the embedded signature of the ISO's md5sum is
# additionally verified against that certificate with "checkisomd5 --cert"
# (optionally validating the certificate's trust chain via --ca / --CApath).
# The signature is embedded inside the ISO, so no detached .sig file is needed.
cdrom=$(cat /proc/sys/dev/cdrom/info | grep 'drive name' | awk '{print $3}') # e.g. sr0
[[ -z "$cdrom" ]] && return  # when string is empty just return

# check if cdrom is loaded and readable
file -s /dev/$cdrom &>/dev/null || return  # if no cdrom is mounted just return

if type -p checkisomd5 &>/dev/null; then
    # only perform the test if the executable checkisomd5 is present
    # see also script /usr/share/rear/output/ISO/Linux-i386/840_implantisomd5.sh

    # Build the checkisomd5 option list: always --verbose; add signature
    # verification when a signing certificate is configured and present in the
    # rescue system (copied there by 840_implantisomd5.sh via COPY_AS_IS).
    check_opts=( --verbose )
    if [[ -n "$ISO_SIGNING_CERT" && -r "$ISO_SIGNING_CERT" ]] ; then
        check_opts+=( --cert "$ISO_SIGNING_CERT" )
        [[ -n "$ISO_SIGNING_CA_FILE" && -r "$ISO_SIGNING_CA_FILE" ]] && check_opts+=( --ca "$ISO_SIGNING_CA_FILE" )
        [[ -n "$ISO_SIGNING_CA_DIR"  && -d "$ISO_SIGNING_CA_DIR"  ]] && check_opts+=( --CApath "$ISO_SIGNING_CA_DIR" )
        echo "**** Checking the md5sum and embedded signature of the cdrom or iso image ****"
    else
        echo "**** Checking the md5sum of the cdrom or iso image ****"
    fi
    checkisomd5 "${check_opts[@]}" /dev/$cdrom
fi
