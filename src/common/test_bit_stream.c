#include <stdio.h>
#include "bit_stream.h"

int main() {
    BitWriter writer;
    bitWriterInit(&writer);

    // Writes exactly 8 bits (a full byte, no padding expected): 1 0 1 1 0 0 1 0
    bitWriterWriteBits(&writer, 0b101, 3);
    bitWriterWriteBits(&writer, 0b1, 1);
    bitWriterWriteBits(&writer, 0b0010, 4);

    uint8_t paddingBits = bitWriterFlush(&writer);
    printf("Bytes written: %zu, padding bits: %u\n", writer.byteCount, paddingBits);

    BitReader reader;
    bitReaderInit(&reader, writer.buffer, writer.byteCount);

    printf("Bits read back: ");
    for (int i = 0; i < 8; i++) {
        printf("%d", bitReaderReadBit(&reader));
    }
    printf("\n");

    bitWriterFree(&writer);
    return 0;
}
