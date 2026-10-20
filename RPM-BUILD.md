# Building RPM Packages

This document describes how to build RPM packages for isomd5sum.

## Quick Start

```bash
# Build both source and binary RPMs:
make rpms

# Build source RPM only:
make srpm

# Build binary RPMs only:
make rpm
```

## Prerequisites

Install the build dependencies:

```bash
# Fedora / RHEL / CentOS:
sudo dnf install rpm-build gcc make popt-devel openssl-devel python3-devel

# Or use the spec file to install build deps:
sudo dnf builddep isomd5sum.spec
```

## What Gets Built

The build produces three RPM packages:

1. **isomd5sum** — main package with command-line tools
   - `/usr/bin/implantisomd5`
   - `/usr/bin/checkisomd5`
   - Man pages

2. **isomd5sum-devel** — development headers and static libraries
   - `/usr/include/libimplantisomd5.h`
   - `/usr/include/libcheckisomd5.h`
   - `/usr/lib64/libimplantisomd5.a`
   - `/usr/lib64/libcheckisomd5.a`
   - `/usr/share/pkgconfig/isomd5sum.pc`

3. **python3-isomd5sum** — Python 3 bindings
   - Python module: `pyisomd5sum.so`

## Customizing the Build

### Change the RPM topdir

By default, RPMs are built in `~/rpmbuild`. You can override this:

```bash
make rpms RPMTOPDIR=/tmp/mybuild
```

### Override rpmbuild command

```bash
make rpms RPMBUILD=/usr/local/bin/rpmbuild
```

## Build Output

After running `make rpms`, you'll find:

- **Source RPM:** `~/rpmbuild/SRPMS/isomd5sum-1.2.6-1.*.src.rpm`
- **Binary RPMs:** `~/rpmbuild/RPMS/*/isomd5sum-*.rpm`

## Installing the Built RPMs

```bash
sudo dnf install ~/rpmbuild/RPMS/x86_64/isomd5sum-*.rpm
```

## Troubleshooting

If the build fails with missing dependencies, install them:

```bash
sudo dnf install popt-devel openssl-devel python3-devel
```

To see what the build would do without executing:

```bash
make -n rpms
```
