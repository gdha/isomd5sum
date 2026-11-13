# The Makefile compiles the C binaries with -O3 and no -g, so no useful
# debuginfo is produced. Disable the automatic -debuginfo/-debugsource
# subpackages to avoid the fatal "Empty %files file debugsourcefiles.list"
# error on EL9+. Because rpm's automatic stripping is part of that same
# debuginfo machinery, we strip the installed binaries/objects ourselves in
# the install section to avoid rpmlint's "unstripped-binary-or-object"
# warning.
%global debug_package %{nil}

# This is an enhanced, drop-in replacement for the upstream "isomd5sum"
# package. The RPM is renamed to "rear-isomd5sum" so it is distinguishable
# from (and supersedes) the distribution package, but every installed
# artifact - the implantisomd5/checkisomd5 binaries, the libraries, headers,
# pkg-config file and the python3 isomd5sum module - keeps its upstream name
# so existing consumers (e.g. Anaconda) continue to work unchanged.
# upstream_name is the name of the source tree/tarball, which is unchanged.
%global upstream_name isomd5sum

Name:           rear-isomd5sum
Version:        1.2.6
Release:        1%{?dist}
Summary:        Utilities for working with md5sum implanted in ISO images (rear drop-in)

# The sources are "GPL version 2 or (at your option) any later version", i.e.
# GPLv2+. We use the legacy Fedora license short name here because EL9's
# rpmlint (1.x) does not yet recognise SPDX identifiers like GPL-2.0-or-later.
License:        GPLv2+
URL:            https://github.com/rhinstaller/isomd5sum
Source0:        %{upstream_name}-%{version}.tar.bz2

# Drop-in replacement for the stock isomd5sum: provide its capability and
# obsolete older stock builds so this package is chosen in its place.
Provides:       isomd5sum = %{version}-%{release}
Obsoletes:      isomd5sum < %{version}-%{release}

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  popt-devel
BuildRequires:  openssl-devel
BuildRequires:  python3-devel

%description
isomd5sum provides a way of making use of the ISO9660 application data
area to store md5sum data about an ISO image.  This allows you to check
the ISO given nothing more than the image itself.

In addition to the md5sum, the serial number of an X.509 certificate can
optionally be embedded into the same application data area.  The embedded
md5sum can also be signed with a private key so that consumers can prove
the checksum was produced by the holder of the certificate.  The signing
certificate can optionally be validated against trusted CA certificate(s)
(checkisomd5 --ca <bundle> and/or --CApath <directory>) before its key is
trusted.

This is an enhanced, drop-in replacement for the stock "isomd5sum" package:
it installs the same binaries, libraries and python module and simply adds
the certificate-serial, signing and CA trust-chain features.

%package devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}
Provides:       isomd5sum-devel = %{version}-%{release}
Obsoletes:      isomd5sum-devel < %{version}-%{release}

%description devel
Header files and static libraries for developing applications that use
the isomd5sum library.

%package -n python3-%{upstream_name}
Summary:        Python 3 bindings for %{upstream_name}
Requires:       %{name} = %{version}-%{release}
# Do NOT use %%python_provide here: that macro is not defined in every build
# environment (e.g. some RHEL 8 setups), where it fails with
# "Unknown tag: %%python_provide".
#
# rpm already auto-generates the python-isomd5sum, python3-isomd5sum and
# python3.<minor>-isomd5sum (dotted) Provides, so we must NOT repeat those or
# rpmlint reports "useless-provides". The only name not generated automatically
# is the interpreter-versioned DOTLESS form (e.g. python39-isomd5sum), which is
# what some dependencies use; provide just that. python3_version_nodots is e.g.
# "39" on RHEL 9 and "36" on RHEL 8, so this stays correct on both.
Provides:       python%{python3_version_nodots}-%{upstream_name} = %{version}-%{release}

%description -n python3-%{upstream_name}
Python 3 bindings for the isomd5sum library.

%prep
%autosetup -n %{upstream_name}-%{version}

%build
%make_build

%install
%make_install

# Strip installed ELF objects ourselves. With debug_package disabled (above)
# rpm does not run its usual strip pass, which otherwise leaves rpmlint
# reporting "unstripped-binary-or-object". We keep the .a archives intact
# (stripping a static library would remove symbols needed to link against it).
strip %{buildroot}%{_bindir}/implantisomd5 %{buildroot}%{_bindir}/checkisomd5
strip %{buildroot}%{python3_sitearch}/pyisomd5sum.so

%files
%license COPYING
%doc README
%{_bindir}/implantisomd5
%{_bindir}/checkisomd5
%{_mandir}/man1/implantisomd5.1*
%{_mandir}/man1/checkisomd5.1*

%files devel
%{_includedir}/libimplantisomd5.h
%{_includedir}/libcheckisomd5.h
%{_libdir}/libimplantisomd5.a
%{_libdir}/libcheckisomd5.a
%{_datadir}/pkgconfig/isomd5sum.pc

%files -n python3-%{upstream_name}
%{python3_sitearch}/pyisomd5sum.so

%changelog
* %(date "+%a %b %d %Y") %{?packager}%{!?packager:Packager} - %{version}-%{release}
- Correct License to GPLv2+ (sources are "v2 or any later version"); the
  previous GPL-2.0-only was both inaccurate and unrecognised by EL9 rpmlint
- Fix outdated FSF postal address in COPYING (rpmlint incorrect-fsf-address)
- Strip installed binaries and the python .so in %%install (debug_package is
  disabled, so rpm's own strip pass does not run) to clear
  unstripped-binary-or-object
- Replace %%python_provide (undefined on some RHEL 8 build hosts, where it
  fails with "Unknown tag") with an explicit dotless python%%{nodots}-isomd5sum
  Provides; the dotted/unversioned names are left to rpm's auto-provides to
  avoid "useless-provides". Fixes the RHEL 8 build and the earlier EL9
  obsolete-not-provided warning.
- Rename package to rear-isomd5sum as an enhanced drop-in replacement for the
  stock isomd5sum (Provides/Obsoletes isomd5sum); installed binaries, libs,
  headers, pkg-config and the python3 isomd5sum module keep their names
- Add optional "checkisomd5 --ca <file>" trust-chain validation: the signing
  certificate is run through X509_verify_cert against the supplied CA bundle
  before its key is trusted (reports "Signature status: UNTRUSTED" on failure)
- Add "checkisomd5 --CApath <dir>" as an alternative/complement to --ca,
  accepting a c_rehash CA directory (openssl "-CApath" style); either or both
  may be given
- Integrate upstream PRs #41-#45: fix endless loop on missing PVD, signed
  integer overflow, 32-bit big-endian number parsing, progress callback
  overrun, and terminal settings guard when stdin is not a tty
