CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2

all: banker

banker: banker.c
	$(CC) $(CFLAGS) -o $@ banker.c

clean:
	rm -f banker *.o

.PHONY: all clean
