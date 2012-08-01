PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
CFLAGS=-Wall -Werror
LDFLAGS=
OS=$(shell uname -s | tr A-Z a-z)
INSTALL=install
TAR=tar
TARG=tasque
MOFILE=main.o
OFILES=\
	conn.o\
	heap.o\
	job.o\
	set.o\
	net.o\
	srv.o\
	times.o\
	tube.o\
	event.o\
	hash.o\
	dlist.o

all: $(TARG)
.PHONY: all

$(TARG): $(OFILES) $(MOFILE)
	$(LINK.o) -o $@ $^ $(LDLIBS)

install: $(BINDIR) $(BINDIR)/$(TARG)
.PHONY: install

$(BINDIR):
	$(INSTALL) -d $@

$(BINDIR)/%: %
	$(INSTALL) $< $@

CLEANFILES:=$(CLEANFILES) $(TARG)

$(OFILES) $(MOFILE): $(HFILES)

clean:
	rm -f *.o $(CLEANFILES)
.PHONY: clean
