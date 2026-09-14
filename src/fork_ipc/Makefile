CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDLIBS = -lcrypto

COMMON_DIR = ../common
COMMON_SOURCES = $(COMMON_DIR)/file_format.c $(COMMON_DIR)/md5_utils.c \
                  $(COMMON_DIR)/huffman.c $(COMMON_DIR)/min_heap.c \
                  $(COMMON_DIR)/bit_stream.c $(COMMON_DIR)/file_scanner.c \
                  $(COMMON_DIR)/compression_pipeline.c $(COMMON_DIR)/work_partition.c

all: fork_compressor fork_decompressor

fork_compressor: fork_compressor.c pipe_buffer.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

fork_decompressor: fork_decompressor.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f fork_compressor fork_decompressor

.PHONY: all clean
