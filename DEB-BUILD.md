# Building DEB Packages

This document describes how to build Debian/Ubuntu (.deb) packages for
isomd5sum.

## Quick Start

```bash
# Build binary .deb packages:
make deb
```

## Prerequisites

Install the build dependencies:

```bash
# Debian / Ubuntu:
sudo apt install build-essential debhelper-compat dh-python \
    libpopt-dev libssl-dev python3-dev
```

Or read the build dependencies directly from the packaging:

```bash
sudo apt build-deb .
```

## What Gets Built

The build produces three binary packages plus debug-symbol packages:

1. **rear-isomd5sum** — main package with command-line tools
   - `/usr/bin/implantisomd5`
   - `/usr/bin/checkisomd5`
   - Man pages, README, COPYING

2. **rear-isomd5sum-dev** — development headers and static libraries
   - `/usr/include/libimplantisomd5.h`
   - `/usr/include/libcheckisomd5.h`
   - `/usr/lib/libimplantisomd5.a`
   - `/usr/lib/libcheckisomd5.a`
   - `/usr/share/pkgconfig/isomd5sum.pc`

3. **python3-isomd5sum** — Python 3 bindings
   - `pyisomd5sum.cpython-3xx-x86_64-linux-gnu.so`

All three packages are drop-in replacements for the stock `isomd5sum`
packages (Provides/Conflicts/Replaces).

## Build Output

After running `make deb`, the build artifacts are in `./debbuild/`:

```
debbuild/
├── rear-isomd5sum_1.2.6-1_amd64.deb
├── rear-isomd5sum-dev_1.2.6-1_amd64.deb
├── python3-isomd5sum_1.2.6-1_amd64.deb
├── rear-isomd5sum-dbgsym_1.2.6-1_amd64.ddeb
├── python3-isomd5sum-dbgsym_1.2.6-1_amd64.ddeb
├── rear-isomd5sum_1.2.6-1_amd64.buildinfo
├── rear-isomd5sum_1.2.6-1_amd64.changes
└── isomd5sum-1.2.6/          (extracted source tree)
```

## Customizing the Build

### Change the build output directory

```bash
make deb DEBTOPDIR=/tmp/mybuild
```

### Inspect package contents

```bash
dpkg-deb -c debbuild/rear-isomd5sum_1.2.6-1_amd64.deb
dpkg-deb -I debbuild/python3-isomd5sum_1.2.6-1_amd64.deb
```

### Run lintian checks

```bash
lintian debbuild/*.deb
```

## Installing the Built Packages

```bash
sudo apt install ./debbuild/rear-isomd5sum_1.2.6-1_amd64.deb \
                 ./debbuild/python3-isomd5sum_1.2.6-1_amd64.deb
# (install rear-isomd5sum-dev too if you need the headers/static libs)
```

## Cleaning Up

```bash
make clean    # removes build artifacts and debbuild/
```
