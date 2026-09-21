#include "md5_utils.h"

#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>

bool computeFileMd5(const char *filePath,
                    uint8_t digestOutput[MD5_DIGEST_LENGTH]) {
  FILE *inputFile = fopen(filePath, "rb");
  if (inputFile == NULL) {
    fprintf(stderr, "computeFileMd5: could not open '%s'\n", filePath);
    return false;
  }

  // EVP_MD_CTX carries the running hash state across each chunk we
  // feed it, so we never need the whole file in memory at once.
  EVP_MD_CTX *hashContext = EVP_MD_CTX_new();
  if (hashContext == NULL) {
    fprintf(stderr, "computeFileMd5: failed to allocate EVP_MD_CTX\n");
    fclose(inputFile);
    return false;
  }

  bool wasSuccessful = false;
  if (EVP_DigestInit_ex(hashContext, EVP_md5(), NULL) == 1) {
    uint8_t readBuffer[MD5_READ_BUFFER_SIZE];
    size_t bytesRead;
    bool updateFailed = false;

    while ((bytesRead = fread(readBuffer, 1, sizeof(readBuffer), inputFile)) >
           0) {
      if (EVP_DigestUpdate(hashContext, readBuffer, bytesRead) != 1) {
        updateFailed = true;
        break;
      }
    }

    if (!updateFailed && !ferror(inputFile)) {
      unsigned int digestLength = 0;
      if (EVP_DigestFinal_ex(hashContext, digestOutput, &digestLength) == 1 &&
          digestLength == MD5_DIGEST_LENGTH) {
        wasSuccessful = true;
      }
    }
  }

  if (!wasSuccessful) {
    fprintf(stderr, "computeFileMd5: hashing failed for '%s'\n", filePath);
  }

  EVP_MD_CTX_free(hashContext);
  fclose(inputFile);
  return wasSuccessful;
}

bool digestsAreEqual(const uint8_t firstDigest[MD5_DIGEST_LENGTH],
                     const uint8_t secondDigest[MD5_DIGEST_LENGTH]) {
  return memcmp(firstDigest, secondDigest, MD5_DIGEST_LENGTH) == 0;
}

void digestToHexString(const uint8_t digest[MD5_DIGEST_LENGTH],
                       char outputHex[33]) {
  for (int byteIndex = 0; byteIndex < MD5_DIGEST_LENGTH; byteIndex++) {
    sprintf(outputHex + (byteIndex * 2), "%02x", digest[byteIndex]);
  }
  outputHex[32] = '\0';
}
