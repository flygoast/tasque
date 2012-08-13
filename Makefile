PREFIX=/usr/local/
BINDIR=$(PREFIX)/bin
CFLAGS=-Wall -Werror -g
LDFLAGS=
OS=$(shell uname -s | tr A-Z a-z)
INSTALL=install

TARG=tasque
MOFILE=main.o
VERS=version.h
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

all: $(VERS) $(TARG)
.PHONY: all

$(VERS):
	sh version.h.sh

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
	rm -f *.o $(CLEANFILES) $(VERS)
.PHONY: clean
