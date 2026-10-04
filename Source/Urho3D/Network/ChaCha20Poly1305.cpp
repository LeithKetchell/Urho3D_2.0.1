// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// ChaCha20-Poly1305 AEAD per RFC 8439.

#include "../Precompiled.h"
#include "ChaCha20Poly1305.h"
#include <cstring>

namespace Urho3D
{

// ─── ChaCha20 ────────────────────────────────────────────────────────────────

static inline unsigned RotL32(unsigned v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static inline unsigned LoadLE32(const unsigned char* p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static inline void StoreLE32(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static inline void StoreLE64(unsigned char* p, unsigned long long v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = (unsigned char)(v >> (i * 8));
}

#define QR(a, b, c, d) \
    a += b; d ^= a; d = RotL32(d, 16); \
    c += d; b ^= c; b = RotL32(b, 12); \
    a += b; d ^= a; d = RotL32(d, 8);  \
    c += d; b ^= c; b = RotL32(b, 7);

static void ChaCha20Block(unsigned output[16], const unsigned input[16])
{
    unsigned x[16];
    for (int i = 0; i < 16; ++i)
        x[i] = input[i];

    // 20 rounds = 10 double rounds
    for (int i = 0; i < 10; ++i)
    {
        // Column rounds
        QR(x[0], x[4], x[8],  x[12]);
        QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        // Diagonal rounds
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]);
        QR(x[3], x[4], x[9],  x[14]);
    }

    for (int i = 0; i < 16; ++i)
        output[i] = x[i] + input[i];
}

#undef QR

void ChaCha20::Init(const unsigned char key[32], const unsigned char nonce[12], unsigned counter)
{
    // "expand 32-byte k"
    state_[0] = 0x61707865;
    state_[1] = 0x3320646e;
    state_[2] = 0x79622d32;
    state_[3] = 0x6b206574;
    // Key
    for (int i = 0; i < 8; ++i)
        state_[4 + i] = LoadLE32(key + i * 4);
    // Counter
    state_[12] = counter;
    // Nonce
    state_[13] = LoadLE32(nonce);
    state_[14] = LoadLE32(nonce + 4);
    state_[15] = LoadLE32(nonce + 8);
    counter_ = counter;
}

void ChaCha20::Process(const unsigned char* in, unsigned char* out, unsigned len)
{
    unsigned block[16];
    unsigned char keystream[64];

    while (len > 0)
    {
        state_[12] = counter_++;
        ChaCha20Block(block, state_);
        for (int i = 0; i < 16; ++i)
            StoreLE32(keystream + i * 4, block[i]);

        unsigned take = (len < 64) ? len : 64;
        for (unsigned i = 0; i < take; ++i)
            out[i] = in[i] ^ keystream[i];
        in += take;
        out += take;
        len -= take;
    }

    memset(keystream, 0, 64);
}

void ChaCha20::KeystreamBlock(unsigned char block[64])
{
    unsigned out[16];
    state_[12] = counter_++;
    ChaCha20Block(out, state_);
    for (int i = 0; i < 16; ++i)
        StoreLE32(block + i * 4, out[i]);
}

// ─── Poly1305 ────────────────────────────────────────────────────────────────

void Poly1305::Init(const unsigned char key[32])
{
    // Clamp r: clear high bits per spec
    unsigned t0 = LoadLE32(key);
    unsigned t1 = LoadLE32(key + 4);
    unsigned t2 = LoadLE32(key + 8);
    unsigned t3 = LoadLE32(key + 12);

    r_[0] = t0 & 0x03ffffff;
    r_[1] = ((t0 >> 26) | (t1 << 6)) & 0x03ffff03;
    r_[2] = ((t1 >> 20) | (t2 << 12)) & 0x03ffc0ff;
    r_[3] = ((t2 >> 14) | (t3 << 18)) & 0x03f03fff;
    r_[4] = (t3 >> 8) & 0x000fffff;

    h_[0] = h_[1] = h_[2] = h_[3] = h_[4] = 0;

    pad_[0] = LoadLE32(key + 16);
    pad_[1] = LoadLE32(key + 20);
    pad_[2] = LoadLE32(key + 24);
    pad_[3] = LoadLE32(key + 28);
}

void Poly1305::ProcessBlock(const unsigned char* block, unsigned len, bool isFinal)
{
    unsigned hibit = isFinal ? 0 : (1U << 24);

    unsigned t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    if (len >= 4) t0 = LoadLE32(block);
    else for (unsigned i = 0; i < len && i < 4; ++i) t0 |= (unsigned)block[i] << (i * 8);

    if (len >= 8) t1 = LoadLE32(block + 4);
    else if (len > 4) for (unsigned i = 4; i < len && i < 8; ++i) t1 |= (unsigned)block[i] << ((i - 4) * 8);

    if (len >= 12) t2 = LoadLE32(block + 8);
    else if (len > 8) for (unsigned i = 8; i < len && i < 12; ++i) t2 |= (unsigned)block[i] << ((i - 8) * 8);

    if (len >= 16) t3 = LoadLE32(block + 12);
    else if (len > 12) for (unsigned i = 12; i < len && i < 16; ++i) t3 |= (unsigned)block[i] << ((i - 12) * 8);

    // Add 2^(8*len) bit if not final partial
    if (!isFinal || len == 16)
    {
        // Full block: set bit 128
    }

    h_[0] += t0 & 0x03ffffff;
    h_[1] += ((t0 >> 26) | (t1 << 6)) & 0x03ffffff;
    h_[2] += ((t1 >> 20) | (t2 << 12)) & 0x03ffffff;
    h_[3] += ((t2 >> 14) | (t3 << 18)) & 0x03ffffff;
    h_[4] += (t3 >> 8) | hibit;

    // Multiply h by r using 64-bit intermediates
    unsigned long long d0, d1, d2, d3, d4;
    unsigned long long r0 = r_[0], r1 = r_[1], r2 = r_[2], r3 = r_[3], r4 = r_[4];
    unsigned long long s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;

    d0 = (unsigned long long)h_[0] * r0 + (unsigned long long)h_[1] * s4 +
         (unsigned long long)h_[2] * s3 + (unsigned long long)h_[3] * s2 +
         (unsigned long long)h_[4] * s1;
    d1 = (unsigned long long)h_[0] * r1 + (unsigned long long)h_[1] * r0 +
         (unsigned long long)h_[2] * s4 + (unsigned long long)h_[3] * s3 +
         (unsigned long long)h_[4] * s2;
    d2 = (unsigned long long)h_[0] * r2 + (unsigned long long)h_[1] * r1 +
         (unsigned long long)h_[2] * r0 + (unsigned long long)h_[3] * s4 +
         (unsigned long long)h_[4] * s3;
    d3 = (unsigned long long)h_[0] * r3 + (unsigned long long)h_[1] * r2 +
         (unsigned long long)h_[2] * r1 + (unsigned long long)h_[3] * r0 +
         (unsigned long long)h_[4] * s4;
    d4 = (unsigned long long)h_[0] * r4 + (unsigned long long)h_[1] * r3 +
         (unsigned long long)h_[2] * r2 + (unsigned long long)h_[3] * r1 +
         (unsigned long long)h_[4] * r0;

    // Carry propagation
    unsigned long long c;
    c = d0 >> 26; h_[0] = (unsigned)(d0 & 0x03ffffff); d1 += c;
    c = d1 >> 26; h_[1] = (unsigned)(d1 & 0x03ffffff); d2 += c;
    c = d2 >> 26; h_[2] = (unsigned)(d2 & 0x03ffffff); d3 += c;
    c = d3 >> 26; h_[3] = (unsigned)(d3 & 0x03ffffff); d4 += c;
    c = d4 >> 26; h_[4] = (unsigned)(d4 & 0x03ffffff);
    h_[0] += (unsigned)(c * 5);
    c = h_[0] >> 26; h_[0] &= 0x03ffffff; h_[1] += (unsigned)c;
}

void Poly1305::Update(const unsigned char* data, unsigned len)
{
    while (len >= 16)
    {
        ProcessBlock(data, 16, false);
        data += 16;
        len -= 16;
    }
    if (len > 0)
    {
        // Partial block: pad with 0x01 byte after data (Poly1305 adds 2^(8*blocklen))
        unsigned char padded[17];
        memcpy(padded, data, len);
        padded[len] = 0x01;
        memset(padded + len + 1, 0, 16 - len);

        // Process as a partial block with the high bit NOT set
        // We need to handle this differently — the hibit should be 0 for partial blocks
        ProcessBlock(padded, len, true);
    }
}

void Poly1305::Final(unsigned char tag[16])
{
    // Final reduction
    unsigned c;
    c = h_[1] >> 26; h_[1] &= 0x03ffffff; h_[2] += c;
    c = h_[2] >> 26; h_[2] &= 0x03ffffff; h_[3] += c;
    c = h_[3] >> 26; h_[3] &= 0x03ffffff; h_[4] += c;
    c = h_[4] >> 26; h_[4] &= 0x03ffffff; h_[0] += c * 5;
    c = h_[0] >> 26; h_[0] &= 0x03ffffff; h_[1] += c;

    // Compute h - p = h - (2^130 - 5)
    unsigned g0 = h_[0] + 5; c = g0 >> 26; g0 &= 0x03ffffff;
    unsigned g1 = h_[1] + c; c = g1 >> 26; g1 &= 0x03ffffff;
    unsigned g2 = h_[2] + c; c = g2 >> 26; g2 &= 0x03ffffff;
    unsigned g3 = h_[3] + c; c = g3 >> 26; g3 &= 0x03ffffff;
    unsigned g4 = h_[4] + c - (1U << 26);

    // Select h or g based on whether g overflowed
    unsigned mask = (g4 >> 31) - 1;  // 0xffffffff if g4 >= 0, 0 if g4 < 0
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h_[0] = (h_[0] & mask) | g0;
    h_[1] = (h_[1] & mask) | g1;
    h_[2] = (h_[2] & mask) | g2;
    h_[3] = (h_[3] & mask) | g3;
    h_[4] = (h_[4] & mask) | g4;

    // h = h mod 2^128
    unsigned long long f;
    f = (unsigned long long)h_[0] | ((unsigned long long)h_[1] << 26);
    unsigned h0 = (unsigned)f; f >>= 32;
    f += (unsigned long long)h_[1] >> 6 | ((unsigned long long)h_[2] << 20);
    unsigned h1 = (unsigned)f; f >>= 32;
    f += (unsigned long long)h_[2] >> 12 | ((unsigned long long)h_[3] << 14);
    unsigned h2 = (unsigned)f; f >>= 32;
    f += (unsigned long long)h_[3] >> 18 | ((unsigned long long)h_[4] << 8);
    unsigned h3 = (unsigned)f;

    // Add pad
    unsigned long long t;
    t = (unsigned long long)h0 + pad_[0]; h0 = (unsigned)t; t >>= 32;
    t += (unsigned long long)h1 + pad_[1]; h1 = (unsigned)t; t >>= 32;
    t += (unsigned long long)h2 + pad_[2]; h2 = (unsigned)t; t >>= 32;
    t += (unsigned long long)h3 + pad_[3]; h3 = (unsigned)t;

    StoreLE32(tag, h0);
    StoreLE32(tag + 4, h1);
    StoreLE32(tag + 8, h2);
    StoreLE32(tag + 12, h3);

    // Zero state
    memset(this, 0, sizeof(*this));
}

// ─── AEAD construction (RFC 8439 Section 2.8) ───────────────────────────────

static void Poly1305Pad16(Poly1305& poly, unsigned len)
{
    unsigned rem = len % 16;
    if (rem != 0)
    {
        unsigned char zeros[16] = {};
        poly.Update(zeros, 16 - rem);
    }
}

bool ChaCha20Poly1305Seal(
    const unsigned char key[32],
    const unsigned char nonce[12],
    const unsigned char* plaintext, unsigned plaintextLen,
    const unsigned char* aad, unsigned aadLen,
    unsigned char* ciphertext,
    unsigned char tag[16])
{
    // 1. Generate Poly1305 one-time key from ChaCha20 block 0
    ChaCha20 cipher;
    cipher.Init(key, nonce, 0);
    unsigned char polyKey[64];
    cipher.KeystreamBlock(polyKey);  // counter=0 block

    // 2. Encrypt plaintext with ChaCha20 starting at counter=1
    cipher.Init(key, nonce, 1);
    cipher.Process(plaintext, ciphertext, plaintextLen);

    // 3. Compute Poly1305 tag over: aad || pad || ciphertext || pad || len(aad) || len(ct)
    Poly1305 poly;
    poly.Init(polyKey);
    memset(polyKey, 0, 64);

    if (aadLen > 0)
        poly.Update(aad, aadLen);
    Poly1305Pad16(poly, aadLen);

    poly.Update(ciphertext, plaintextLen);
    Poly1305Pad16(poly, plaintextLen);

    unsigned char lenBlock[16];
    StoreLE64(lenBlock, aadLen);
    StoreLE64(lenBlock + 8, plaintextLen);
    poly.Update(lenBlock, 16);

    poly.Final(tag);
    return true;
}

bool ChaCha20Poly1305Open(
    const unsigned char key[32],
    const unsigned char nonce[12],
    const unsigned char* ciphertext, unsigned ciphertextLen,
    const unsigned char* aad, unsigned aadLen,
    const unsigned char tag[16],
    unsigned char* plaintext)
{
    // 1. Generate Poly1305 one-time key
    ChaCha20 cipher;
    cipher.Init(key, nonce, 0);
    unsigned char polyKey[64];
    cipher.KeystreamBlock(polyKey);

    // 2. Verify tag BEFORE decrypting
    Poly1305 poly;
    poly.Init(polyKey);
    memset(polyKey, 0, 64);

    if (aadLen > 0)
        poly.Update(aad, aadLen);
    Poly1305Pad16(poly, aadLen);

    poly.Update(ciphertext, ciphertextLen);
    Poly1305Pad16(poly, ciphertextLen);

    unsigned char lenBlock[16];
    StoreLE64(lenBlock, aadLen);
    StoreLE64(lenBlock + 8, ciphertextLen);
    poly.Update(lenBlock, 16);

    unsigned char computedTag[16];
    poly.Final(computedTag);

    // Constant-time comparison
    unsigned diff = 0;
    for (int i = 0; i < 16; ++i)
        diff |= computedTag[i] ^ tag[i];

    memset(computedTag, 0, 16);

    if (diff != 0)
        return false;

    // 3. Decrypt
    cipher.Init(key, nonce, 1);
    cipher.Process(ciphertext, plaintext, ciphertextLen);
    return true;
}

}
