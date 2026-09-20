CC = gcc
CFLAGS = -Wall -Wextra -std=c11 `pkg-config --cflags gtk+-3.0`
LDLIBS = -lpthread `pkg-config --libs gtk+-3.0`

all: gutenpress test_process_runner

gutenpress: main.c process_runner.c panel_compress.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_process_runner: test_process_runner.c process_runner.c
	gcc -Wall -Wextra -std=c11 $^ -o $@ -lpthread

clean:
	rm -f gutenpress test_process_runner

.PHONY: all clean
