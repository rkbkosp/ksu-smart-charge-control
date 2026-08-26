SHELL := /bin/sh

CC ?= cc
CFLAGS ?= -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wconversion -Wshadow -Wformat=2
SANITIZERS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SOURCES := test_charged.c ../native/config.c ../native/policy.c ../native/wifi.c

.PHONY: all run clean

all: test_charged

test_charged: $(SOURCES) $(wildcard ../native/*.h)
	$(CC) $(CFLAGS) $(SANITIZERS) -I../native $(SOURCES) -o $@

run: test_charged
	./test_charged

clean:
	rm -rf test_charged test_charged.dSYM
