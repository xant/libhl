UNAME := $(shell uname)

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


#CC = gcc
TARGETS = $(patsubst %.c, %.o, $(wildcard src/*.c))
TESTS = $(patsubst %.c, %, $(wildcard test/*.c))

all: objects static shared tests

static: objects
	ar -r libhl.a src/*.o

shared: objects
	$(CC) $(LDFLAGS) $(SHAREDFLAGS) src/*.o -o libhl.$(SHAREDEXT)

objects: CFLAGS += -fPIC -Isrc -Wno-parentheses -Wno-pointer-sign -O3
objects: $(TARGETS)

clean:
	rm -f src/*.o
	rm -f test/*_test
	rm -f libhl.a
	rm -f libhl.$(SHAREDEXT)
	rm -f support/testing.o

support/testing.o:
	$(CC) -Isrc -c support/testing.c -o support/testing.o

tests: CFLAGS += -Isrc -Isupport -Wno-parentheses -Wno-pointer-sign -O3 -L. support/testing.o

tests: static support/testing.o 
	@for i in $(TESTS); do\
	  echo "$(CC) $(CFLAGS) $$i.c -o $$i libhl.a $(LDFLAGS)";\
	  $(CC) $(CFLAGS) $$i.c -o $$i libhl.a $(LDFLAGS);\
	done
