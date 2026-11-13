PYTHON ?= python3
PYTHONSITEPACKAGES := $(shell $(PYTHON) -c 'import site; print(site.getsitepackages()[0])')
PYTHONINCLUDE := $(shell $(PYTHON)-config --includes)

VERSION=1.2.6

ifneq (,$(filter sparc64 ppc64 ppc64le x86_64 s390x aarch64 mips64 riscv64,$(shell uname -m)))
LIBDIR = lib64
else
LIBDIR = lib
endif

CFLAGS += -std=gnu11 -Wall -D_GNU_SOURCE=1 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE=1 -D_LARGEFILE64_SOURCE=1 -fPIC $(PYTHONINCLUDE)

# Security hardening. Overridable (e.g. "make HARDENING=") and composes with
# distro build flags (RPM %optflags already sets most of these). Full RELRO +
# stack protector + FORTIFY + format-string warnings as errors. Note: we do
# NOT add -fPIE here because the same object files are also linked into the
# shared module pyisomd5sum.so; the -fPIC already in CFLAGS is compatible with
# both a PIE executable (via -pie at link time) and a shared object.
# _FORTIFY_SOURCE requires optimization, which the %.o rule provides (-O3).
HARDENING ?= -fstack-protector-strong -fstack-clash-protection \
             -D_FORTIFY_SOURCE=2 \
             -Wformat -Wformat-security -Werror=format-security
CFLAGS += $(HARDENING)

OBJECTS = md5.o libimplantisomd5.o checkisomd5.o certificate.o signature.o implantisomd5
SOURCES = $(patsubst %.o,%.c,$(OBJECTS))
LDFLAGS += -fPIC
# Link hardening safe for both executables and the shared module.
LDFLAGS += -Wl,-z,relro,-z,now -Wl,-z,noexecstack
# PIE applies only to the executables (not the .so / .a). Overridable with the
# HARDENING knob above being cleared, or directly.
PIE ?= -pie

PYOBJS = pyisomd5sum.o libcheckisomd5.a libimplantisomd5.a

all: implantisomd5 checkisomd5 pyisomd5sum.so libimplantisomd5.a libcheckisomd5.a

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -O3 -o $@ $<

implantisomd5: implantisomd5.o libimplantisomd5.a
	$(CC) $(CPPFLAGS) $(CFLAGS) implantisomd5.o libimplantisomd5.a -lpopt -lcrypto $(LDFLAGS) $(PIE) -o implantisomd5

checkisomd5: checkisomd5.o libcheckisomd5.a
	$(CC) $(CPPFLAGS) $(CFLAGS) checkisomd5.o libcheckisomd5.a -lpopt -lcrypto $(LDFLAGS) $(PIE) -o checkisomd5

IMPLANT_AR_OBJS = libimplantisomd5.o md5.o utilities.o certificate.o signature.o
CHECK_AR_OBJS   = libcheckisomd5.o md5.o utilities.o certificate.o signature.o

libimplantisomd5.a: $(IMPLANT_AR_OBJS)
	rm -f $@
	$(AR) rcs $@ $(IMPLANT_AR_OBJS)

libcheckisomd5.a: $(CHECK_AR_OBJS)
	rm -f $@
	$(AR) rcs $@ $(CHECK_AR_OBJS)

pyisomd5sum.so: $(PYOBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -g -fpic $(PYOBJS) -lcrypto $(LDFLAGS) -o pyisomd5sum.so

install: all install-bin install-python install-devel

install-bin:
	install -d -m 0755 $(DESTDIR)/usr/bin
	install -d -m 0755 $(DESTDIR)/usr/share/man/man1
	install -m 0755 implantisomd5 $(DESTDIR)/usr/bin
	install -m 0755 checkisomd5 $(DESTDIR)/usr/bin
	install -m 0644 implantisomd5.1 $(DESTDIR)/usr/share/man/man1
	install -m 0644 checkisomd5.1 $(DESTDIR)/usr/share/man/man1

install-python:
	install -d -m 0755 $(DESTDIR)$(PYTHONSITEPACKAGES)
	install -m 0755 pyisomd5sum.so $(DESTDIR)$(PYTHONSITEPACKAGES)

install-devel:
	install -d -m 0755 $(DESTDIR)/usr/include
	install -d -m 0755 $(DESTDIR)/usr/$(LIBDIR)
	install -d -m 0755 $(DESTDIR)/usr/share/pkgconfig
	install -m 0644 libimplantisomd5.h $(DESTDIR)/usr/include/
	install -m 0644 libcheckisomd5.h $(DESTDIR)/usr/include/
	install -m 0644 libimplantisomd5.a $(DESTDIR)/usr/$(LIBDIR)
	install -m 0644 libcheckisomd5.a $(DESTDIR)/usr/$(LIBDIR)
	sed "s#@VERSION@#${VERSION}#g; s#@includedir@#/usr/include#g; s#@libdir@#/usr/${LIBDIR}#g" isomd5sum.pc.in > ${DESTDIR}/usr/share/pkgconfig/isomd5sum.pc

clean:
	rm -f *.o *.so *.pyc *.a .depend *~
	rm -f implantisomd5 checkisomd5
	rm -rf $(CURDIR)/debbuild

tag:
	@git tag -a -m "Tag as $(VERSION)" -f $(VERSION)
	@echo "Tagged as $(VERSION)"

archive:
	@git archive --format=tar --prefix=isomd5sum-$(VERSION)/ HEAD |bzip2 > isomd5sum-$(VERSION).tar.bz2
	@echo "The final archive is in isomd5sum-$(VERSION).tar.bz2"

# ── RPM packaging ─────────────────────────────────────────────────────────────
RPMBUILD    ?= rpmbuild
RPMTOPDIR   ?= $(HOME)/rpmbuild
SRCRPMDIR    = $(RPMTOPDIR)/SRPMS
RPMDIR       = $(RPMTOPDIR)/RPMS
SPECFILE     = isomd5sum.spec

# Prepare the rpmbuild tree and drop the source tarball in place.
rpmprep: archive
	@mkdir -p $(RPMTOPDIR)/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
	@cp isomd5sum-$(VERSION).tar.bz2 $(RPMTOPDIR)/SOURCES/
	@cp $(SPECFILE) $(RPMTOPDIR)/SPECS/
	@echo "rpmbuild tree ready in $(RPMTOPDIR)"

# Build a source RPM only.
srpm: rpmprep
	$(RPMBUILD) -bs \
		--define "_topdir $(RPMTOPDIR)" \
		$(RPMTOPDIR)/SPECS/$(SPECFILE)
	@echo "SRPM written to $(SRCRPMDIR)"

# Build binary RPMs (requires build dependencies installed).
rpm: rpmprep
	$(RPMBUILD) -bb \
		--define "_topdir $(RPMTOPDIR)" \
		$(RPMTOPDIR)/SPECS/$(SPECFILE)
	@echo "RPMs written to $(RPMDIR)"

# Build both source and binary RPMs.
rpms: rpmprep
	$(RPMBUILD) -ba \
		--define "_topdir $(RPMTOPDIR)" \
		$(RPMTOPDIR)/SPECS/$(SPECFILE)
	@echo "RPMs written to $(RPMDIR), SRPM to $(SRCRPMDIR)"

# ── DEB packaging ─────────────────────────────────────────────────────────────
DEBBUILD   ?= dpkg-buildpackage
DEBTOPDIR  ?= $(CURDIR)/debbuild
DEBSRCDIR   = $(DEBTOPDIR)/isomd5sum-$(VERSION)

# Prepare the deb build tree: extract a clean source tarball and overlay the
# debian/ packaging directory on top of it.  The tarball (from "archive") does
# not contain debian/ unless it has been committed, so we copy it explicitly.
debprep: archive
	rm -rf $(DEBTOPDIR)
	mkdir -p $(DEBTOPDIR)
	tar xjf isomd5sum-$(VERSION).tar.bz2 -C $(DEBTOPDIR)
	cp -r debian $(DEBSRCDIR)/debian
	@echo "deb build tree ready in $(DEBSRCDIR)"

# Build binary .deb packages (requires build dependencies installed).
# dpkg-buildpackage leaves the .debs in the parent of the source tree, i.e.
# $(DEBTOPDIR).  -b = binary-only, -us -uc = skip signing, --no-pre-clean =
# do not run debian/rules clean first (the tree is already clean from the
# tarball extraction).
deb: debprep
	cd $(DEBSRCDIR) && $(DEBBUILD) -b -us -uc --no-pre-clean
	@echo "DEBs written to $(DEBTOPDIR)"

test:
	$(PYTHON) ./testpyisomd5sum.py
