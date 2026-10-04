// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// X25519 Diffie-Hellman per RFC 7748.
// Field arithmetic on GF(2^255 - 19) using 64-bit limbs.

#include "../Precompiled.h"
#include "X25519.h"
#include "CryptoRNG.h"
#include <cstring>

namespace Urho3D
{

// ─── Field element: 5 × 51-bit limbs representing a value mod 2^255 - 19 ────

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned __int128 u128;

struct Fe
{
    u64 v[5];
};

static void FeZero(Fe& f)
{
    f.v[0] = f.v[1] = f.v[2] = f.v[3] = f.v[4] = 0;
}

static void FeOne(Fe& f)
{
    f.v[0] = 1;
    f.v[1] = f.v[2] = f.v[3] = f.v[4] = 0;
}

static void FeCopy(Fe& dst, const Fe& src)
{
    for (int i = 0; i < 5; ++i)
        dst.v[i] = src.v[i];
}

static void FeAdd(Fe& h, const Fe& f, const Fe& g)
{
    for (int i = 0; i < 5; ++i)
        h.v[i] = f.v[i] + g.v[i];
}

static void FeSub(Fe& h, const Fe& f, const Fe& g)
{
    // Add 2p to avoid underflow before subtraction
    static const u64 two_p[5] = {
        0xFFFFFFFFFFFDA, 0xFFFFFFFFFFFFE, 0xFFFFFFFFFFFFE,
        0xFFFFFFFFFFFFE, 0xFFFFFFFFFFFFE
    };
    for (int i = 0; i < 5; ++i)
        h.v[i] = f.v[i] + two_p[i] - g.v[i];
}

static void FeCarry(Fe& h)
{
    u64 c;
    for (int i = 0; i < 4; ++i)
    {
        c = h.v[i] >> 51;
        h.v[i] &= (1ULL << 51) - 1;
        h.v[i + 1] += c;
    }
    c = h.v[4] >> 51;
    h.v[4] &= (1ULL << 51) - 1;
    h.v[0] += c * 19;
}

static void FeMul(Fe& h, const Fe& f, const Fe& g)
{
    u128 t[5];
    // Full multiplication with reduction by 19
    u64 f0 = f.v[0], f1 = f.v[1], f2 = f.v[2], f3 = f.v[3], f4 = f.v[4];
    u64 g0 = g.v[0], g1 = g.v[1], g2 = g.v[2], g3 = g.v[3], g4 = g.v[4];

    u64 g1_19 = g1 * 19, g2_19 = g2 * 19, g3_19 = g3 * 19, g4_19 = g4 * 19;

    t[0] = (u128)f0 * g0 + (u128)f1 * g4_19 + (u128)f2 * g3_19 + (u128)f3 * g2_19 + (u128)f4 * g1_19;
    t[1] = (u128)f0 * g1 + (u128)f1 * g0    + (u128)f2 * g4_19 + (u128)f3 * g3_19 + (u128)f4 * g2_19;
    t[2] = (u128)f0 * g2 + (u128)f1 * g1    + (u128)f2 * g0    + (u128)f3 * g4_19 + (u128)f4 * g3_19;
    t[3] = (u128)f0 * g3 + (u128)f1 * g2    + (u128)f2 * g1    + (u128)f3 * g0    + (u128)f4 * g4_19;
    t[4] = (u128)f0 * g4 + (u128)f1 * g3    + (u128)f2 * g2    + (u128)f3 * g1    + (u128)f4 * g0;

    u128 c;
    c = t[0] >> 51; h.v[0] = (u64)(t[0] & ((1ULL << 51) - 1)); t[1] += c;
    c = t[1] >> 51; h.v[1] = (u64)(t[1] & ((1ULL << 51) - 1)); t[2] += c;
    c = t[2] >> 51; h.v[2] = (u64)(t[2] & ((1ULL << 51) - 1)); t[3] += c;
    c = t[3] >> 51; h.v[3] = (u64)(t[3] & ((1ULL << 51) - 1)); t[4] += c;
    c = t[4] >> 51; h.v[4] = (u64)(t[4] & ((1ULL << 51) - 1));
    h.v[0] += (u64)c * 19;
    c = (u128)(h.v[0] >> 51);
    h.v[0] &= (1ULL << 51) - 1;
    h.v[1] += (u64)c;
}

static void FeSq(Fe& h, const Fe& f)
{
    FeMul(h, f, f);
}

static void FeMul121666(Fe& h, const Fe& f)
{
    // Multiply by the constant 121666 = (A-2)/4 for Curve25519
    u128 t[5];
    for (int i = 0; i < 5; ++i)
        t[i] = (u128)f.v[i] * 121666;

    u128 c;
    c = t[0] >> 51; h.v[0] = (u64)(t[0] & ((1ULL << 51) - 1)); t[1] += c;
    c = t[1] >> 51; h.v[1] = (u64)(t[1] & ((1ULL << 51) - 1)); t[2] += c;
    c = t[2] >> 51; h.v[2] = (u64)(t[2] & ((1ULL << 51) - 1)); t[3] += c;
    c = t[3] >> 51; h.v[3] = (u64)(t[3] & ((1ULL << 51) - 1)); t[4] += c;
    c = t[4] >> 51; h.v[4] = (u64)(t[4] & ((1ULL << 51) - 1));
    h.v[0] += (u64)c * 19;
}

// Conditional swap: swap f and g if swap != 0 (constant time)
static void FeCSwap(Fe& f, Fe& g, u64 swap)
{
    u64 mask = (u64)(-(s64)swap);
    for (int i = 0; i < 5; ++i)
    {
        u64 x = mask & (f.v[i] ^ g.v[i]);
        f.v[i] ^= x;
        g.v[i] ^= x;
    }
}

// Field inversion: compute f^(p-2) mod p where p = 2^255 - 19
// Uses Fermat's little theorem via addition chain
static void FeInvert(Fe& out, const Fe& z)
{
    Fe t0, t1, t2, t3;

    FeSq(t0, z);        // z^2
    FeSq(t1, t0);       // z^4
    FeSq(t1, t1);       // z^8
    FeMul(t1, z, t1);   // z^9
    FeMul(t0, t0, t1);  // z^11
    FeSq(t2, t0);       // z^22
    FeMul(t1, t1, t2);  // z^31 = z^(2^5-1)

    FeSq(t2, t1);
    for (int i = 1; i < 5; ++i) FeSq(t2, t2);
    FeMul(t1, t2, t1);  // z^(2^10-1)

    FeSq(t2, t1);
    for (int i = 1; i < 10; ++i) FeSq(t2, t2);
    FeMul(t2, t2, t1);  // z^(2^20-1)

    FeSq(t3, t2);
    for (int i = 1; i < 20; ++i) FeSq(t3, t3);
    FeMul(t2, t3, t2);  // z^(2^40-1)

    FeSq(t2, t2);
    for (int i = 1; i < 10; ++i) FeSq(t2, t2);
    FeMul(t1, t2, t1);  // z^(2^50-1)

    FeSq(t2, t1);
    for (int i = 1; i < 50; ++i) FeSq(t2, t2);
    FeMul(t2, t2, t1);  // z^(2^100-1)

    FeSq(t3, t2);
    for (int i = 1; i < 100; ++i) FeSq(t3, t3);
    FeMul(t2, t3, t2);  // z^(2^200-1)

    FeSq(t2, t2);
    for (int i = 1; i < 50; ++i) FeSq(t2, t2);
    FeMul(t1, t2, t1);  // z^(2^250-1)

    FeSq(t1, t1);
    FeSq(t1, t1);       // z^(2^252-4)
    FeMul(out, t1, z);   // z^(2^252-3) but we need z^(p-2) = z^(2^255-21)

    // Actually the correct exponent for p-2 = 2^255-21
    // Let me redo this properly...

    // Start over with the standard addition chain for 2^255-21
    // z^(p-2) where p = 2^255-19, so p-2 = 2^255-21
    // The exponent in binary is: 11111...101011 (250 ones, then specific low bits)
    // Standard chain:
    // z^1, z^2, z^(2^2-1), z^(2^5-1), z^(2^10-1), z^(2^20-1), z^(2^40-1),
    // z^(2^50-1), z^(2^100-1), z^(2^200-1), z^(2^250-1), z^(2^255-21)

    // Re-do from scratch with correct chain
    Fe z2, z9, z11, z_5_0, z_10_0, z_20_0, z_40_0, z_50_0, z_100_0;

    FeSq(z2, z);             // z^2
    FeSq(t0, z2);            // z^4
    FeSq(t0, t0);            // z^8
    FeMul(z9, t0, z);        // z^9
    FeMul(z11, z9, z2);      // z^11
    FeSq(t0, z11);           // z^22
    FeMul(z_5_0, z9, t0);    // z^(2^5-1) = z^31

    FeSq(t0, z_5_0);
    for (int i = 1; i < 5; ++i) FeSq(t0, t0);
    FeMul(z_10_0, t0, z_5_0);

    FeSq(t0, z_10_0);
    for (int i = 1; i < 10; ++i) FeSq(t0, t0);
    FeMul(z_20_0, t0, z_10_0);

    FeSq(t0, z_20_0);
    for (int i = 1; i < 20; ++i) FeSq(t0, t0);
    FeMul(t0, t0, z_20_0);  // z^(2^40-1)
    FeCopy(z_40_0, t0);

    FeSq(t0, z_40_0);
    for (int i = 1; i < 10; ++i) FeSq(t0, t0);
    FeMul(z_50_0, t0, z_10_0);

    FeSq(t0, z_50_0);
    for (int i = 1; i < 50; ++i) FeSq(t0, t0);
    FeMul(z_100_0, t0, z_50_0);

    FeSq(t0, z_100_0);
    for (int i = 1; i < 100; ++i) FeSq(t0, t0);
    FeMul(t0, t0, z_100_0);  // z^(2^200-1)

    FeSq(t0, t0);
    for (int i = 1; i < 50; ++i) FeSq(t0, t0);
    FeMul(t0, t0, z_50_0);   // z^(2^250-1)

    FeSq(t0, t0);            // z^(2^251-2)
    FeSq(t0, t0);            // z^(2^252-4)
    FeSq(t0, t0);            // z^(2^253-8)
    FeSq(t0, t0);            // z^(2^254-16)
    FeSq(t0, t0);            // z^(2^255-32)
    FeMul(out, t0, z11);     // z^(2^255-32+11) = z^(2^255-21) = z^(p-2)
}

// ─── Encoding/decoding ───────────────────────────────────────────────────────

static void FeFromBytes(Fe& h, const unsigned char s[32])
{
    u64 h0 = (u64)s[0] | ((u64)s[1] << 8) | ((u64)s[2] << 16) | ((u64)s[3] << 24) |
             ((u64)s[4] << 32) | ((u64)s[5] << 40) | ((u64)(s[6] & 0x07) << 48);
    u64 h1 = ((u64)s[6] >> 3) | ((u64)s[7] << 5) | ((u64)s[8] << 13) | ((u64)s[9] << 21) |
             ((u64)s[10] << 29) | ((u64)s[11] << 37) | ((u64)(s[12] & 0x3f) << 45);
    u64 h2 = ((u64)s[12] >> 6) | ((u64)s[13] << 2) | ((u64)s[14] << 10) | ((u64)s[15] << 18) |
             ((u64)s[16] << 26) | ((u64)s[17] << 34) | ((u64)s[18] << 42) | ((u64)(s[19] & 0x01) << 50);
    u64 h3 = ((u64)s[19] >> 1) | ((u64)s[20] << 7) | ((u64)s[21] << 15) | ((u64)s[22] << 23) |
             ((u64)s[23] << 31) | ((u64)s[24] << 39) | ((u64)(s[25] & 0x0f) << 47);
    u64 h4 = ((u64)s[25] >> 4) | ((u64)s[26] << 4) | ((u64)s[27] << 12) | ((u64)s[28] << 20) |
             ((u64)s[29] << 28) | ((u64)s[30] << 36) | ((u64)(s[31] & 0x7f) << 44);

    h.v[0] = h0;
    h.v[1] = h1;
    h.v[2] = h2;
    h.v[3] = h3;
    h.v[4] = h4;
}

static void FeToBytes(unsigned char s[32], Fe& h)
{
    // Full reduction mod p
    FeCarry(h);
    FeCarry(h);

    // Reduce to [0, p)
    Fe t;
    FeCopy(t, h);
    t.v[0] += 19;
    FeCarry(t);
    // If t.v[4] has bit 51 set, then h >= p, so use t - 2^255 (i.e., keep t with top bit cleared)
    u64 mask = -(t.v[4] >> 51); // 0 if no overflow, all-1s if overflow... actually wrong
    // Simpler approach: fully reduce
    // Add 19, carry, if bit 255 is set then the original was >= p
    // Let's just do it the safe way:

    // Reduce: if h >= p, subtract p
    u64 c = h.v[0] + 19;
    c >>= 51;
    c += h.v[1]; c >>= 51;
    c += h.v[2]; c >>= 51;
    c += h.v[3]; c >>= 51;
    c += h.v[4]; c >>= 51;
    // c is 1 if h >= p, 0 otherwise
    h.v[0] += 19 * c;
    FeCarry(h);
    h.v[4] &= (1ULL << 51) - 1;  // clear bit 255

    // Pack 5×51-bit limbs into 32 bytes, little-endian
    u64 h0 = h.v[0], h1 = h.v[1], h2 = h.v[2], h3 = h.v[3], h4 = h.v[4];

    // Limb 0: bits [0, 51)
    s[0]  = (unsigned char)(h0);
    s[1]  = (unsigned char)(h0 >> 8);
    s[2]  = (unsigned char)(h0 >> 16);
    s[3]  = (unsigned char)(h0 >> 24);
    s[4]  = (unsigned char)(h0 >> 32);
    s[5]  = (unsigned char)(h0 >> 40);
    s[6]  = (unsigned char)((h0 >> 48) | (h1 << 3));
    s[7]  = (unsigned char)(h1 >> 5);
    s[8]  = (unsigned char)(h1 >> 13);
    s[9]  = (unsigned char)(h1 >> 21);
    s[10] = (unsigned char)(h1 >> 29);
    s[11] = (unsigned char)(h1 >> 37);
    s[12] = (unsigned char)((h1 >> 45) | (h2 << 6));
    s[13] = (unsigned char)(h2 >> 2);
    s[14] = (unsigned char)(h2 >> 10);
    s[15] = (unsigned char)(h2 >> 18);
    s[16] = (unsigned char)(h2 >> 26);
    s[17] = (unsigned char)(h2 >> 34);
    s[18] = (unsigned char)(h2 >> 42);
    s[19] = (unsigned char)((h2 >> 50) | (h3 << 1));
    s[20] = (unsigned char)(h3 >> 7);
    s[21] = (unsigned char)(h3 >> 15);
    s[22] = (unsigned char)(h3 >> 23);
    s[23] = (unsigned char)(h3 >> 31);
    s[24] = (unsigned char)(h3 >> 39);
    s[25] = (unsigned char)((h3 >> 47) | (h4 << 4));
    s[26] = (unsigned char)(h4 >> 4);
    s[27] = (unsigned char)(h4 >> 12);
    s[28] = (unsigned char)(h4 >> 20);
    s[29] = (unsigned char)(h4 >> 28);
    s[30] = (unsigned char)(h4 >> 36);
    s[31] = (unsigned char)(h4 >> 44);
}

// ─── Montgomery ladder: scalar multiplication on Curve25519 ──────────────────

// X25519 scalar multiplication per RFC 7748 Section 5
static void ScalarMult(unsigned char result[32],
                       const unsigned char scalar[32],
                       const unsigned char point[32])
{
    // Decode scalar with clamping (RFC 7748 specifies caller clamps, but we do it here for safety)
    unsigned char e[32];
    memcpy(e, scalar, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;

    // Decode u-coordinate
    Fe u;
    FeFromBytes(u, point);

    // Montgomery ladder
    Fe x_1, x_2, z_2, x_3, z_3;
    FeCopy(x_1, u);
    FeOne(x_2);
    FeZero(z_2);
    FeCopy(x_3, u);
    FeOne(z_3);

    u64 swap = 0;
    for (int pos = 254; pos >= 0; --pos)
    {
        u64 bit = (e[pos >> 3] >> (pos & 7)) & 1;
        swap ^= bit;
        FeCSwap(x_2, x_3, swap);
        FeCSwap(z_2, z_3, swap);
        swap = bit;

        Fe A, AA, B, BB, E, C, D, DA, CB;
        FeAdd(A, x_2, z_2);
        FeSq(AA, A);
        FeSub(B, x_2, z_2);
        FeSq(BB, B);
        FeSub(E, AA, BB);
        FeAdd(C, x_3, z_3);
        FeSub(D, x_3, z_3);
        FeMul(DA, D, A);
        FeMul(CB, C, B);

        Fe sum, diff;
        FeAdd(sum, DA, CB);
        FeSq(x_3, sum);
        FeSub(diff, DA, CB);
        FeSq(diff, diff);
        FeMul(z_3, x_1, diff);

        FeMul(x_2, AA, BB);
        Fe a24E;
        FeMul121666(a24E, E);
        FeAdd(a24E, a24E, AA);
        FeMul(z_2, E, a24E);
    }
    FeCSwap(x_2, x_3, swap);
    FeCSwap(z_2, z_3, swap);

    // result = x_2 * z_2^(-1)
    Fe zInv;
    FeInvert(zInv, z_2);
    Fe res;
    FeMul(res, x_2, zInv);
    FeToBytes(result, res);
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool X25519GenerateKey(unsigned char secretKey[32])
{
    if (!CryptoRandomBytes(secretKey, 32))
        return false;
    // Clamping per RFC 7748
    secretKey[0] &= 248;
    secretKey[31] &= 127;
    secretKey[31] |= 64;
    return true;
}

void X25519PublicKey(unsigned char publicKey[32], const unsigned char secretKey[32])
{
    // Basepoint = 9
    unsigned char basepoint[32] = {};
    basepoint[0] = 9;
    ScalarMult(publicKey, secretKey, basepoint);
}

bool X25519SharedSecret(unsigned char sharedSecret[32],
                        const unsigned char secretKey[32],
                        const unsigned char peerPublicKey[32])
{
    ScalarMult(sharedSecret, secretKey, peerPublicKey);

    // Check for all-zero output (low-order point)
    unsigned acc = 0;
    for (int i = 0; i < 32; ++i)
        acc |= sharedSecret[i];
    return acc != 0;
}

}
