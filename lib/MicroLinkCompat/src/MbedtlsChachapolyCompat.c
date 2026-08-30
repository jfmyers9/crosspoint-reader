/*
 * PioArduino's prebuilt X4 Pro core exposes the Mbed TLS ChaChaPoly headers but
 * omits the implementation. MicroLink uses only the one-shot API, so bridge it
 * to the ChaCha20-Poly1305 implementation already bundled with wireguard-lwip.
 */

#include <stdint.h>
#include <string.h>

#include "crypto/refc/chacha20poly1305.h"
#include "mbedtls/chachapoly.h"

_Static_assert(sizeof(mbedtls_chachapoly_context) >= 32, "ChaChaPoly context cannot hold the compatibility key");

static unsigned char* compat_key(mbedtls_chachapoly_context* ctx) { return (unsigned char*)ctx; }

static uint64_t compat_nonce(const unsigned char nonce[12]) {
  uint64_t value = 0;
  for (unsigned int i = 0; i < 8; ++i) {
    value |= (uint64_t)nonce[4 + i] << (8 * i);
  }
  return value;
}

void mbedtls_chachapoly_init(mbedtls_chachapoly_context* ctx) {
  if (ctx != NULL) {
    memset(ctx, 0, sizeof(*ctx));
  }
}

void mbedtls_chachapoly_free(mbedtls_chachapoly_context* ctx) {
  if (ctx != NULL) {
    memset(ctx, 0, sizeof(*ctx));
  }
}

int mbedtls_chachapoly_setkey(mbedtls_chachapoly_context* ctx, const unsigned char key[32]) {
  if (ctx == NULL || key == NULL) {
    return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
  }
  memcpy(compat_key(ctx), key, 32);
  return 0;
}

int mbedtls_chachapoly_encrypt_and_tag(mbedtls_chachapoly_context* ctx, size_t length, const unsigned char nonce[12],
                                       const unsigned char* aad, size_t aad_len, const unsigned char* input,
                                       unsigned char* output, unsigned char tag[16]) {
  if (ctx == NULL || nonce == NULL || output == NULL || tag != output + length) {
    return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
  }
  chacha20poly1305_encrypt(output, input, length, aad, aad_len, compat_nonce(nonce), compat_key(ctx));
  return 0;
}

int mbedtls_chachapoly_auth_decrypt(mbedtls_chachapoly_context* ctx, size_t length, const unsigned char nonce[12],
                                    const unsigned char* aad, size_t aad_len, const unsigned char tag[16],
                                    const unsigned char* input, unsigned char* output) {
  if (ctx == NULL || nonce == NULL || input == NULL || (length > 0 && output == NULL) || tag != input + length) {
    return MBEDTLS_ERR_CHACHAPOLY_BAD_STATE;
  }
  unsigned char emptyOutput;
  unsigned char* decryptOutput = length == 0 ? &emptyOutput : output;
  return chacha20poly1305_decrypt(decryptOutput, input, length + 16, aad, aad_len, compat_nonce(nonce), compat_key(ctx))
             ? 0
             : MBEDTLS_ERR_CHACHAPOLY_AUTH_FAILED;
}
