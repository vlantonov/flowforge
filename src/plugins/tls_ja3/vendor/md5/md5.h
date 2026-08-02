/*
 * md5.h — public-domain MD5 implementation.
 * Implements MD5_CTX, MD5Init, MD5Update, MD5Final as defined in RFC 1321.
 * This implementation is placed in the public domain.
 */
#ifndef MD5_H
#define MD5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t      lo, hi;        /* message bit count */
    uint32_t      a, b, c, d;   /* state */
    unsigned char buffer[64];    /* input buffer */
    uint32_t      block[16];     /* processed block (scratch) */
} MD5_CTX;

void MD5Init(MD5_CTX *ctx);
void MD5Update(MD5_CTX *ctx, const void *data, unsigned long size);
void MD5Final(unsigned char *result, MD5_CTX *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MD5_H */
