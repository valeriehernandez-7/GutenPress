CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDLIBS = -lcrypto -lpthread

COMMON_DIR = ../common
COMMON_SOURCES = $(COMMON_DIR)/file_format.c $(COMMON_DIR)/md5_utils.c \
                  $(COMMON_DIR)/huffman.c $(COMMON_DIR)/min_heap.c \
                  $(COMMON_DIR)/bit_stream.c $(COMMON_DIR)/file_scanner.c \
                  $(COMMON_DIR)/compression_pipeline.c $(COMMON_DIR)/work_partition.c \
                  $(COMMON_DIR)/growable_buffer.c $(COMMON_DIR)/directory_utils.c

all: pthread_compressor pthread_decompressor

pthread_compressor: pthread_compressor.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

pthread_decompressor: pthread_decompressor.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f pthread_compressor pthread_decompressor

distclean: clean
	rm -f pthread_test_output.huff
	rm -rf pthread_extracted_output

.PHONY: all clean distclean
