# Builds every smoke test in common/. Each target lists only the .c files
# that specific test actually needs, so compiling one test never pulls in
# unrelated dependencies (e.g. test_min_heap does not need bit_stream.c).
#
# Note: huffman.c depends on bit_stream.c (encodeBuffer/decodeBuffer use
# BitWriter/BitReader), so every target that links huffman.c must also
# link bit_stream.c, even if that particular test doesn't call those
# functions directly.

CC = gcc
CFLAGS = -Wall -Wextra -std=c11
LDLIBS = -lcrypto

TESTS = test_min_heap test_bit_stream test_huffman_tree test_canonical_codes test_encode_decode test_md5 test_file_format

all: $(TESTS)

test_min_heap: test_min_heap.c min_heap.c
	$(CC) $(CFLAGS) $^ -o $@

test_bit_stream: test_bit_stream.c bit_stream.c
	$(CC) $(CFLAGS) $^ -o $@

test_huffman_tree: test_huffman_tree.c huffman.c min_heap.c bit_stream.c
	$(CC) $(CFLAGS) $^ -o $@

test_canonical_codes: test_canonical_codes.c huffman.c min_heap.c bit_stream.c
	$(CC) $(CFLAGS) $^ -o $@

test_encode_decode: test_encode_decode.c huffman.c min_heap.c bit_stream.c
	$(CC) $(CFLAGS) $^ -o $@

test_md5: test_md5.c md5_utils.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test_file_format: test_file_format.c file_format.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TESTS) test_archive.huff

.PHONY: all clean
