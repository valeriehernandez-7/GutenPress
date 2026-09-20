CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDLIBS = -lpthread

all: test_process_runner

test_process_runner: test_process_runner.c process_runner.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f test_process_runner

.PHONY: all clean
