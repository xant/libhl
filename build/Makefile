SHELL = /bin/sh


builddir = .
top_srcdir = .
srcdir = .
prefix = /usr/local
exec_prefix = ${prefix}
bindir = $(exec_prefix)/bin
infodir = $(prefix)/info
libdir = $(prefix)/lib
includedir = $(prefix)/include
mandir = $(prefix)/man/man1

CC = gcc
CPPFLAGS = 
CFLAGS = $(CPPFLAGS) -g -O2
LDFLAGS = 
LIBS = 
INSTALL = @INSTALL@

UNAME := $(shell uname)

LDFLAGS += -L.

ifeq ($(UNAME), Linux)
LDFLAGS += -pthread
else
LDFLAGS +=
endif

ifeq ($(UNAME), Darwin)
SHAREDFLAGS = -dynamiclib
SHAREDEXT = dylib
else
SHAREDFLAGS = -shared
SHAREDEXT = so
endif

LIBDIR = $(libdir)
ifeq ("$(LIBDIR)", "")
LIBDIR=/usr/local/lib
endif

INCDIR = $(includedir)
ifeq ("$(INCDIR)", "")
INCDIR=/usr/local/include
endif

IS_CLANG := $(shell $(CC) --version | grep clang)

ifneq ("$(IS_CLANG)", "")
CLANG_FLAGS=-Wno-undefined-inline -Wno-unknown-warning-option
endif

TARGETS = $(patsubst $(srcdir)/src/%.c, $(builddir)/%.o, $(wildcard $(srcdir)/src/*.c))
TESTS = $(patsubst %.c, %, $(wildcard $(top_srcdir)/test/*_test.c))

TEST_EXEC_ORDER = fbuf_test \
		  rbuf_test \
		  linklist_test \
		  hashtable_test \
		  rqueue_test \
		  queue_test \
		  rbtree_test \
		  avltree_test \
		  binheap_test \
		  pqueue_test \
		  skiplist_test \
		  trie_test

all: objects static shared

.PHONY: static
static: objects
	ar -r libhl.a *.o

.PHONY: shared
shared: objects
	$(CC) $(LDFLAGS) $(SHAREDFLAGS) $(builddir)/*.o -o $(builddir)/libhl.$(SHAREDEXT)

%.o : $(srcdir)/src/%.c
	$(CC) -c $(CFLAGS) $< -o $(builddir)/$@

objects: CFLAGS += -fPIC -I$(top_srcdir)/src -Wall -Werror -Wno-parentheses -Wno-pointer-sign -Wno-unused-function $(CLANG_FLAGS) -DTHREAD_SAFE -g -O3
objects: $(TARGETS)

.PHONY: clean
clean:
	@echo "Cleaning libhl"
	rm -f *.o
	rm -f test/*_test
	rm -f libhl.a
	rm -f libhl.$(SHAREDEXT)
	@if [ -f $(top_srcdir)/support/libut/libut.a ]; then \
	    echo "Cleaning libut"; \
	    make -C $(top_srcdir)/support/libut clean; \
	fi


.PHONY: distclean
distclean: clean
	rm config.h Makefile config.status config.log

.PHONY: libut
libut:
	@if [ ! -f $(top_srcdir)/support/libut/Makefile ]; then git submodule init; git submodule update; fi; make -C $(top_srcdir)/support/libut

.PHONY:tests
tests: CFLAGS += -I$(top_srcdir)/src -I$(top_srcdir)/support/libut/src -Wall -Werror -Wno-parentheses -Wno-pointer-sign -Wno-unused-function $(CLANG_FLAGS) -DTHREAD_SAFE -g -O3
tests: libut static
	@for i in $(TESTS); do\
	  echo "$(CC) $(CFLAGS) $$i.c -o $$i libhl.a $(LDFLAGS) -lm";\
	  $(CC) $(CFLAGS) $$i.c -o $$i libhl.a $(top_srcdir)/support/libut/libut.a $(LDFLAGS) -lm;\
	done;\
	for i in $(TEST_EXEC_ORDER); do echo; $(top_srcdir)/test/$$i; echo; done

.PHONY: test
test: tests

.PHONY: install
install:
	 @echo "Installing libraries in $(LIBDIR)"; \
	 cp -v libhl.a $(LIBDIR)/;\
	 cp -v libhl.$(SHAREDEXT) $(LIBDIR)/;\
	 echo "Installing headers in $(INCDIR)"; \
	 cp -v src/*.h $(INCDIR)/;

.PHONY: docs
docs:
	@doxygen libhl.doxycfg

# automatic re-running of configure if the ocnfigure.in file has changed
${srcdir}/configure: configure.in aclocal.m4
	cd ${srcdir} && autoconf

# autoheader might not change config.h.in, so touch a stamp file
${srcdir}/config.h.in: stamp-h.in
${srcdir}/stamp-h.in: configure.in aclocal.m4
	cd ${srcdir} && autoheader
	echo timestamp > ${srcdir}/stamp-h.in

config.h: stamp-h
stamp-h: config.h.in config.status
	./config.status
Makefile: Makefile.in config.status
	./config.status
	config.status: configure
	./config.status --recheck
