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

ifeq ("$(LIBDIR)", "")
LIBDIR=/usr/local/lib
endif

ifeq ("$(INCDIR)", "")
INCDIR=/usr/local/include
endif

#CC = gcc
TARGETS = $(patsubst %.c, %.o, $(wildcard src/*.c))
TESTS = $(patsubst %.c, %, $(wildcard test/*_test.c))

TEST_EXEC_ORDER = fbuf_test \
		  rbuf_test \
		  linklist_test \
		  hashtable_test \
		  rqueue_test \
		  queue_test \
		  rbtree_test \
		  binheap_test \
		  pqueue_test

all: objects static shared

.PHONY: static
static: objects
	ar -r libhl.a src/*.o

.PHONY: shared
shared: objects
	$(CC) $(LDFLAGS) $(SHAREDFLAGS) src/*.o -o libhl.$(SHAREDEXT)

.PHONY: objects
objects: CFLAGS += -fPIC -Isrc -Wall -Werror -Wno-parentheses -Wno-pointer-sign -Wno-unused-function -Wno-undefined-inline -Wno-unknown-warning-option -DTHREAD_SAFE -g -O3
objects: $(TARGETS)

clean:
	rm -f src/*.o
	rm -f test/*_test
	rm -f libhl.a
	rm -f libhl.$(SHAREDEXT)
	@if [ -f support/libut/Makefile ]; then make -C support/libut clean; fi

.PHONY: libut
libut:
	@if [ ! -f support/libut/Makefile ]; then git submodule init; git submodule update; fi; make -C support/libut

.PHONY:tests
tests: CFLAGS += -Isrc -Isupport/libut/src -Wall -Werror -Wno-parentheses -Wno-pointer-sign -DTHREAD_SAFE -g -O3
tests: libut static
	@for i in $(TESTS); do\
	  echo "$(CC) $(CFLAGS) $$i.c -o $$i libhl.a $(LDFLAGS) -lm";\
	  $(CC) $(CFLAGS) $$i.c -o $$i libhl.a support/libut/libut.a $(LDFLAGS) -lm;\
	done;\
	for i in $(TEST_EXEC_ORDER); do echo; test/$$i; echo; done

.PHONY: test
test: tests

install:
	 @echo "Installing libraries in $(LIBDIR)"; \
	 cp -v libhl.a $(LIBDIR)/;\
	 cp -v libhl.$(SHAREDEXT) $(LIBDIR)/;\
	 echo "Installing headers in $(INCDIR)"; \
	 cp -v src/*.h $(INCDIR)/;

.PHONY: docs
docs:
	@doxygen libhl.doxycfg
