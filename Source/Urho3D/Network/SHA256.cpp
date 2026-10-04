// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// SHA-256 implementation per FIPS 180-4.

#include "../Precompiled.h"
#include "SHA256.h"
#include <cstring>

namespace Urho3D
{

// Round constants (first 32 bits of fractional parts of cube roots of first 64 primes)
static const unsigned K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline unsigned RightRotate(unsigned x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static inline unsigned Ch(unsigned x, unsigned y, unsigned z)
{
    return (x & y) ^ (~x & z);
}

static inline unsigned Maj(unsigned x, unsigned y, unsigned z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline unsigned Sigma0(unsigned x)
{
    return RightRotate(x, 2) ^ RightRotate(x, 13) ^ RightRotate(x, 22);
}

static inline unsigned Sigma1(unsigned x)
{
    return RightRotate(x, 6) ^ RightRotate(x, 11) ^ RightRotate(x, 25);
}

static inline unsigned sigma0(unsigned x)
{
    return RightRotate(x, 7) ^ RightRotate(x, 18) ^ (x >> 3);
}

static inline unsigned sigma1(unsigned x)
{
    return RightRotate(x, 17) ^ RightRotate(x, 19) ^ (x >> 10);
}

static inline unsigned LoadBE32(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8)  | (unsigned)p[3];
}

static inline void StoreBE32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)(v);
}

static void SHA256Transform(unsigned state[8], const unsigned char block[64])
{
    unsigned W[64];
    for (int i = 0; i < 16; ++i)
        W[i] = LoadBE32(block + i * 4);
    for (int i = 16; i < 64; ++i)
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];

    unsigned a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i)
    {
        unsigned T1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];
        unsigned T2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void SHA256Init(SHA256Context& ctx)
{
    // Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes)
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
    ctx.bitCount = 0;
    ctx.bufferLen = 0;
}

void SHA256Update(SHA256Context& ctx, const unsigned char* data, unsigned len)
{
    while (len > 0)
    {
        unsigned space = 64 - ctx.bufferLen;
        unsigned take = (len < space) ? len : space;
        memcpy(ctx.buffer + ctx.bufferLen, data, take);
        ctx.bufferLen += take;
        data += take;
        len -= take;
        ctx.bitCount += (unsigned long long)take * 8;

        if (ctx.bufferLen == 64)
        {
            SHA256Transform(ctx.state, ctx.buffer);
            ctx.bufferLen = 0;
        }
    }
}

void SHA256Final(SHA256Context& ctx, unsigned char hash[32])
{
    // Pad: append 0x80, then zeros, then 64-bit big-endian bit count
    ctx.buffer[ctx.bufferLen++] = 0x80;
    if (ctx.bufferLen > 56)
    {
        memset(ctx.buffer + ctx.bufferLen, 0, 64 - ctx.bufferLen);
        SHA256Transform(ctx.state, ctx.buffer);
        ctx.bufferLen = 0;
    }
    memset(ctx.buffer + ctx.bufferLen, 0, 56 - ctx.bufferLen);

    // Append bit count as big-endian 64-bit
    unsigned long long bits = ctx.bitCount;
    ctx.buffer[56] = (unsigned char)(bits >> 56);
    ctx.buffer[57] = (unsigned char)(bits >> 48);
    ctx.buffer[58] = (unsigned char)(bits >> 40);
    ctx.buffer[59] = (unsigned char)(bits >> 32);
    ctx.buffer[60] = (unsigned char)(bits >> 24);
    ctx.buffer[61] = (unsigned char)(bits >> 16);
    ctx.buffer[62] = (unsigned char)(bits >> 8);
    ctx.buffer[63] = (unsigned char)(bits);
    SHA256Transform(ctx.state, ctx.buffer);

    for (int i = 0; i < 8; ++i)
        StoreBE32(hash + i * 4, ctx.state[i]);

    // Zero sensitive state
    memset(&ctx, 0, sizeof(ctx));
}

void SHA256Hash(const unsigned char* data, unsigned len, unsigned char hash[32])
{
    SHA256Context ctx;
    SHA256Init(ctx);
    SHA256Update(ctx, data, len);
    SHA256Final(ctx, hash);
}

void HMACSHA256(const unsigned char* key, unsigned keyLen,
                const unsigned char* data, unsigned dataLen,
                unsigned char mac[32])
{
    // RFC 2104: HMAC(K, m) = H((K ^ opad) || H((K ^ ipad) || m))
    unsigned char keyBlock[64];
    memset(keyBlock, 0, 64);

    if (keyLen > 64)
    {
        // Keys longer than block size are hashed first
        SHA256Hash(key, keyLen, keyBlock);
    }
    else
    {
        memcpy(keyBlock, key, keyLen);
    }

    unsigned char iPad[64], oPad[64];
    for (int i = 0; i < 64; ++i)
    {
        iPad[i] = keyBlock[i] ^ 0x36;
        oPad[i] = keyBlock[i] ^ 0x5c;
    }

    // Inner hash: H(iPad || data)
    unsigned char innerHash[32];
    SHA256Context ctx;
    SHA256Init(ctx);
    SHA256Update(ctx, iPad, 64);
    SHA256Update(ctx, data, dataLen);
    SHA256Final(ctx, innerHash);

    // Outer hash: H(oPad || innerHash)
    SHA256Init(ctx);
    SHA256Update(ctx, oPad, 64);
    SHA256Update(ctx, innerHash, 32);
    SHA256Final(ctx, mac);

    // Zero sensitive material
    memset(keyBlock, 0, 64);
    memset(iPad, 0, 64);
    memset(oPad, 0, 64);
    memset(innerHash, 0, 32);
}

}
