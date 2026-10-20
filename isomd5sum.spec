Name:           isomd5sum
Version:        1.2.6
Release:        1%{?dist}
Summary:        Utilities for working with md5sum implanted in ISO images

License:        GPL-2.0-only
URL:            https://github.com/rhinstaller/isomd5sum
Source0:        %{name}-%{version}.tar.bz2

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
the checksum was produced by the holder of the certificate.

%package devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description devel
Header files and static libraries for developing applications that use
the isomd5sum library.

%package -n python3-%{name}
Summary:        Python 3 bindings for %{name}
Requires:       %{name} = %{version}-%{release}
%{?python_provide:%python_provide python3-%{name}}

%description -n python3-%{name}
Python 3 bindings for the isomd5sum library.

%prep
%autosetup

%build
%make_build

%install
%make_install

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

%files -n python3-%{name}
%{python3_sitearch}/pyisomd5sum.so

%changelog
* %(date "+%a %b %d %Y") %{?packager}%{!?packager:Packager} - %{version}-%{release}
- Integrate upstream PRs #41-#45: fix endless loop on missing PVD, signed
  integer overflow, 32-bit big-endian number parsing, progress callback
  overrun, and terminal settings guard when stdin is not a tty
