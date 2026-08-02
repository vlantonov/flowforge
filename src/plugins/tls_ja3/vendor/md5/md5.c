/*
 * md5.c — public-domain MD5 implementation (RFC 1321).
 * This implementation is placed in the public domain.
 */
#include "md5.h"
#include <string.h>

/* Rotate x left by n bits. */
#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* Basic MD5 auxiliary functions. */
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

/* One MD5 operation. */
#define STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (uint32_t)(t); \
    (a)  = ROL((a), (s));                            \
    (a) += (b);

/* Read 4 bytes in little-endian order without aliasing UB. */
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Process one 64-byte block. */
static void md5_block(MD5_CTX *ctx, const unsigned char *data) {
    uint32_t a = ctx->a, b = ctx->b, c = ctx->c, d = ctx->d;
    uint32_t x[16];
    int i;

    for (i = 0; i < 16; i++)
        x[i] = le32(data + i * 4);

    /* Round 1 */
    STEP(F, a, b, c, d, x[ 0], 0xd76aa478UL,  7)
    STEP(F, d, a, b, c, x[ 1], 0xe8c7b756UL, 12)
    STEP(F, c, d, a, b, x[ 2], 0x242070dbUL, 17)
    STEP(F, b, c, d, a, x[ 3], 0xc1bdceeeUL, 22)
    STEP(F, a, b, c, d, x[ 4], 0xf57c0fafUL,  7)
    STEP(F, d, a, b, c, x[ 5], 0x4787c62aUL, 12)
    STEP(F, c, d, a, b, x[ 6], 0xa8304613UL, 17)
    STEP(F, b, c, d, a, x[ 7], 0xfd469501UL, 22)
    STEP(F, a, b, c, d, x[ 8], 0x698098d8UL,  7)
    STEP(F, d, a, b, c, x[ 9], 0x8b44f7afUL, 12)
    STEP(F, c, d, a, b, x[10], 0xffff5bb1UL, 17)
    STEP(F, b, c, d, a, x[11], 0x895cd7beUL, 22)
    STEP(F, a, b, c, d, x[12], 0x6b901122UL,  7)
    STEP(F, d, a, b, c, x[13], 0xfd987193UL, 12)
    STEP(F, c, d, a, b, x[14], 0xa679438eUL, 17)
    STEP(F, b, c, d, a, x[15], 0x49b40821UL, 22)

    /* Round 2 */
    STEP(G, a, b, c, d, x[ 1], 0xf61e2562UL,  5)
    STEP(G, d, a, b, c, x[ 6], 0xc040b340UL,  9)
    STEP(G, c, d, a, b, x[11], 0x265e5a51UL, 14)
    STEP(G, b, c, d, a, x[ 0], 0xe9b6c7aaUL, 20)
    STEP(G, a, b, c, d, x[ 5], 0xd62f105dUL,  5)
    STEP(G, d, a, b, c, x[10], 0x02441453UL,  9)
    STEP(G, c, d, a, b, x[15], 0xd8a1e681UL, 14)
    STEP(G, b, c, d, a, x[ 4], 0xe7d3fbc8UL, 20)
    STEP(G, a, b, c, d, x[ 9], 0x21e1cde6UL,  5)
    STEP(G, d, a, b, c, x[14], 0xc33707d6UL,  9)
    STEP(G, c, d, a, b, x[ 3], 0xf4d50d87UL, 14)
    STEP(G, b, c, d, a, x[ 8], 0x455a14edUL, 20)
    STEP(G, a, b, c, d, x[13], 0xa9e3e905UL,  5)
    STEP(G, d, a, b, c, x[ 2], 0xfcefa3f8UL,  9)
    STEP(G, c, d, a, b, x[ 7], 0x676f02d9UL, 14)
    STEP(G, b, c, d, a, x[12], 0x8d2a4c8aUL, 20)

    /* Round 3 */
    STEP(H, a, b, c, d, x[ 5], 0xfffa3942UL,  4)
    STEP(H, d, a, b, c, x[ 8], 0x8771f681UL, 11)
    STEP(H, c, d, a, b, x[11], 0x6d9d6122UL, 16)
    STEP(H, b, c, d, a, x[14], 0xfde5380cUL, 23)
    STEP(H, a, b, c, d, x[ 1], 0xa4beea44UL,  4)
    STEP(H, d, a, b, c, x[ 4], 0x4bdecfa9UL, 11)
    STEP(H, c, d, a, b, x[ 7], 0xf6bb4b60UL, 16)
    STEP(H, b, c, d, a, x[10], 0xbebfbc70UL, 23)
    STEP(H, a, b, c, d, x[13], 0x289b7ec6UL,  4)
    STEP(H, d, a, b, c, x[ 0], 0xeaa127faUL, 11)
    STEP(H, c, d, a, b, x[ 3], 0xd4ef3085UL, 16)
    STEP(H, b, c, d, a, x[ 6], 0x04881d05UL, 23)
    STEP(H, a, b, c, d, x[ 9], 0xd9d4d039UL,  4)
    STEP(H, d, a, b, c, x[12], 0xe6db99e5UL, 11)
    STEP(H, c, d, a, b, x[15], 0x1fa27cf8UL, 16)
    STEP(H, b, c, d, a, x[ 2], 0xc4ac5665UL, 23)

    /* Round 4 */
    STEP(I, a, b, c, d, x[ 0], 0xf4292244UL,  6)
    STEP(I, d, a, b, c, x[ 7], 0x432aff97UL, 10)
    STEP(I, c, d, a, b, x[14], 0xab9423a7UL, 15)
    STEP(I, b, c, d, a, x[ 5], 0xfc93a039UL, 21)
    STEP(I, a, b, c, d, x[12], 0x655b59c3UL,  6)
    STEP(I, d, a, b, c, x[ 3], 0x8f0ccc92UL, 10)
    STEP(I, c, d, a, b, x[10], 0xffeff47dUL, 15)
    STEP(I, b, c, d, a, x[ 1], 0x85845dd1UL, 21)
    STEP(I, a, b, c, d, x[ 8], 0x6fa87e4fUL,  6)
    STEP(I, d, a, b, c, x[15], 0xfe2ce6e0UL, 10)
    STEP(I, c, d, a, b, x[ 6], 0xa3014314UL, 15)
    STEP(I, b, c, d, a, x[13], 0x4e0811a1UL, 21)
    STEP(I, a, b, c, d, x[ 4], 0xf7537e82UL,  6)
    STEP(I, d, a, b, c, x[11], 0xbd3af235UL, 10)
    STEP(I, c, d, a, b, x[ 2], 0x2ad7d2bbUL, 15)
    STEP(I, b, c, d, a, x[ 9], 0xeb86d391UL, 21)

    ctx->a += a;
    ctx->b += b;
    ctx->c += c;
    ctx->d += d;

    /* Zero scratch. */
    memset(x, 0, sizeof(x));
}

void MD5Init(MD5_CTX *ctx) {
    ctx->lo = 0;
    ctx->hi = 0;
    ctx->a  = 0x67452301UL;
    ctx->b  = 0xefcdab89UL;
    ctx->c  = 0x98badcfeUL;
    ctx->d  = 0x10325476UL;
}

void MD5Update(MD5_CTX *ctx, const void *data, unsigned long size) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned long saved_lo = ctx->lo;

    if ((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo)
        ctx->hi++;
    ctx->hi += (uint32_t)(size >> 29);

    unsigned long used = saved_lo & 0x3f;

    if (used) {
        unsigned long free_space = 64 - used;
        if (size < free_space) {
            memcpy(ctx->buffer + used, p, size);
            return;
        }
        memcpy(ctx->buffer + used, p, free_space);
        p    += free_space;
        size -= free_space;
        md5_block(ctx, ctx->buffer);
    }

    while (size >= 64) {
        md5_block(ctx, p);
        p    += 64;
        size -= 64;
    }

    if (size)
        memcpy(ctx->buffer, p, size);
}

void MD5Final(unsigned char *result, MD5_CTX *ctx) {
    unsigned long used = ctx->lo & 0x3f;

    ctx->buffer[used++] = 0x80;

    unsigned long free_space = 64 - used;
    if (free_space < 8) {
        memset(ctx->buffer + used, 0, free_space);
        md5_block(ctx, ctx->buffer);
        used = 0;
        free_space = 64;
    }

    memset(ctx->buffer + used, 0, free_space - 8);

    /* Encode bit count in little-endian. */
    uint32_t lo_bits = ctx->lo << 3;
    uint32_t hi_bits = (ctx->hi << 3) | (ctx->lo >> 29);

    ctx->buffer[56] = (unsigned char)(lo_bits);
    ctx->buffer[57] = (unsigned char)(lo_bits >> 8);
    ctx->buffer[58] = (unsigned char)(lo_bits >> 16);
    ctx->buffer[59] = (unsigned char)(lo_bits >> 24);
    ctx->buffer[60] = (unsigned char)(hi_bits);
    ctx->buffer[61] = (unsigned char)(hi_bits >> 8);
    ctx->buffer[62] = (unsigned char)(hi_bits >> 16);
    ctx->buffer[63] = (unsigned char)(hi_bits >> 24);

    md5_block(ctx, ctx->buffer);

    /* Output state as little-endian bytes. */
    result[ 0] = (unsigned char)(ctx->a);
    result[ 1] = (unsigned char)(ctx->a >>  8);
    result[ 2] = (unsigned char)(ctx->a >> 16);
    result[ 3] = (unsigned char)(ctx->a >> 24);
    result[ 4] = (unsigned char)(ctx->b);
    result[ 5] = (unsigned char)(ctx->b >>  8);
    result[ 6] = (unsigned char)(ctx->b >> 16);
    result[ 7] = (unsigned char)(ctx->b >> 24);
    result[ 8] = (unsigned char)(ctx->c);
    result[ 9] = (unsigned char)(ctx->c >>  8);
    result[10] = (unsigned char)(ctx->c >> 16);
    result[11] = (unsigned char)(ctx->c >> 24);
    result[12] = (unsigned char)(ctx->d);
    result[13] = (unsigned char)(ctx->d >>  8);
    result[14] = (unsigned char)(ctx->d >> 16);
    result[15] = (unsigned char)(ctx->d >> 24);

    memset(ctx, 0, sizeof(*ctx));
}
