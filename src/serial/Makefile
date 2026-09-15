CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDLIBS = -lcrypto

COMMON_DIR = ../common
COMMON_SOURCES = $(COMMON_DIR)/file_format.c $(COMMON_DIR)/md5_utils.c \
                  $(COMMON_DIR)/huffman.c $(COMMON_DIR)/min_heap.c \
                  $(COMMON_DIR)/bit_stream.c $(COMMON_DIR)/file_scanner.c \
                  $(COMMON_DIR)/compression_pipeline.c $(COMMON_DIR)/directory_utils.c

all: serial_compressor serial_decompressor

serial_compressor: serial_compressor.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

serial_decompressor: serial_decompressor.c $(COMMON_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f serial_compressor serial_decompressor

distclean: clean
	rm -f test_output.huff
	rm -rf extracted_output

.PHONY: all clean distclean
