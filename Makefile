srcdir = .
prefix = /usr/local
exec_prefix=$(prefix)
bindir=$(exec_prefix)/bin
datadir=$(prefix)/share

CC?=gcc
CFLAGS?= -O2 -s
LDFLAGS?= -lpthread
VERSION?=`grep 'define VERSION' UCI2WB.c | sed -e 's/.*"\(.*\)".*/\1/'`

ALL= uci2wb uci2wb.6.gz

all: ${ALL}

uci2wb: UCI2WB.c
	$(CC) $(CFLAGS) $(CPPFLAGS) UCI2WB.c -o uci2wb $(LDFLAGS)

install: ${ALL}
	install -d -m0755 $(DESTDIR)$(bindir)
	cp -u ${srcdir}/uci2wb $(DESTDIR)$(bindir)
	install -d -m0755 $(DESTDIR)$(datadir)/man/man6
	cp -u ${srcdir}/uci2wb.6.gz $(DESTDIR)$(datadir)/man/man6
	xboard -ncp -uxiAdapter {uci2wb -%variant "%fcp" "%fd"} -autoClose || true

uci2wb.6.gz: uci2wb.pod
	pod2man -s 6 uci2wb.pod | gzip -9n > uci2wb.6.gz

clean:
	rm -f ${ALL}

dist-clean:
	rm -f ${ALL} *~ md5sums

dist:
	install -d -m0755 UCI2WB
	rm -f uci2wb.tar uci2wb.tar.gz
	cp UCI2WB.c UCI2WB.rc rosetta.ico uci2wb.pod Makefile README.txt UCI2WB
	md5sum UCI2WB/* > UCI2WB/md5sums
	tar -cvvf uci2wb-$(VERSION).tar UCI2WB
	gzip uci2wb-$(VERSION).tar
	rm UCI2WB/*
	rmdir UCI2WB

uninstall:
	rm -f $(DESTDIR)$(datadir)/man/man6/uci2wb.6.gz
	rm -f $(DESTDIR)$(bindir)/uci2wb

