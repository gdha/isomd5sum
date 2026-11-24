# 840_implantisomd5.sh - Add a md5sum of the ISO image to the ISO image itself
# We need package 'isomd5sum' to be installed to have this script activated.
#
# When ISO_SIGNING_KEY and ISO_SIGNING_CERT are set, the md5sum is additionally
# signed and the signature is embedded INSIDE the ISO (in the trailing
# skip-sectors region), so there is no detached <iso>.sig file. This requires
# the patched isomd5sum whose implantisomd5 understands --cert/--sign-key and
# whose checkisomd5 understands --cert (see 70-checkisomd5.sh).
IMPLANTISOMD5=$( type -p implantisomd5 )
[[ -x "$IMPLANTISOMD5" ]] || return 0

# To be able to check the md5sum once booted in ReaR rescue image we need also
# the executable 'checkisomd5' (belonging to the same RPM package as implantisomd5).
# E.g. run: checkisomd5 --verbose /dev/sr0
PROGS+=( checkisomd5 )
# Used by /usr/share/rear/skel/default/etc/scripts/system-setup.d/70-checkisomd5.sh

# Build the implantisomd5 option list. Signing is enabled only when both the
# signing key and the certificate are configured and readable.
implant_opts=()
if [[ -n "$ISO_SIGNING_KEY" && -n "$ISO_SIGNING_CERT" ]] ; then
    if [[ -r "$ISO_SIGNING_KEY" && -r "$ISO_SIGNING_CERT" ]] ; then
        implant_opts=( --cert "$ISO_SIGNING_CERT" --sign-key "$ISO_SIGNING_KEY" )
        # The certificate must travel into the rescue system so checkisomd5 can
        # verify the embedded signature at recovery time.
        COPY_AS_IS+=( "$ISO_SIGNING_CERT" )
        [[ -n "$ISO_SIGNING_CA_FILE" && -r "$ISO_SIGNING_CA_FILE" ]] && COPY_AS_IS+=( "$ISO_SIGNING_CA_FILE" )
        [[ -n "$ISO_SIGNING_CA_DIR"  && -d "$ISO_SIGNING_CA_DIR"  ]] && COPY_AS_IS+=( "$ISO_SIGNING_CA_DIR" )
        LogPrint "Embedding md5sum signature into the ISO image(s) using certificate $ISO_SIGNING_CERT"
    else
        LogPrintError "ISO signing requested but $ISO_SIGNING_KEY or $ISO_SIGNING_CERT is not readable - implanting md5sum without a signature"
    fi
fi

# In case we only have 1 single ISO image:
if [[ -f "$ISO_DIR/$ISO_PREFIX.iso" ]] ; then
    LogPrint "Adding md5sum to the ISO image $ISO_DIR/$ISO_PREFIX.iso"
    $IMPLANTISOMD5 "${implant_opts[@]}" "$ISO_DIR/$ISO_PREFIX.iso" >&2
fi

# In case we have multiple ISO images:
isos="$( find ${ISO_DIR} -name "${ISO_PREFIX}_*.iso" -print )"
[[ -z "$isos" ]] && return
LogPrint "Adding md5sum to the ISO images $isos"
find ${ISO_DIR} -name "${ISO_PREFIX}_*.iso" -print -exec $IMPLANTISOMD5 "${implant_opts[@]}" {} \; >&2
