CC = gcc
CFLAGS = -std=c99
LDFLAGS =

CFLAGS += -g # debug
# CFLAGS += -O3 # release

SOURCES = $(wildcard *.c)
HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.c, build/%.o, $(SOURCES))

all: forth

clean:
	rm -f forth build/*.o

tags:
	etags `find . -name "*.h" -o -name "*.c"`

build/%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

forth: $(OBJECTS) $(HEADERS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.PHONY: all clean tags tests
