// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// Hand-rolled SHA-256 — FIPS 180-4 compliant, no third-party library.

#pragma once

namespace Urho3D
{

/// SHA-256 hash context.
struct SHA256Context
{
    unsigned state[8];
    unsigned long long bitCount;
    unsigned char buffer[64];
    unsigned bufferLen;
};

/// Initialize SHA-256 context.
void SHA256Init(SHA256Context& ctx);
/// Feed data into SHA-256.
void SHA256Update(SHA256Context& ctx, const unsigned char* data, unsigned len);
/// Finalize and output 32-byte hash.
void SHA256Final(SHA256Context& ctx, unsigned char hash[32]);

/// Convenience: hash data in one call, output 32 bytes.
void SHA256Hash(const unsigned char* data, unsigned len, unsigned char hash[32]);

/// HMAC-SHA256: keyed hash for key derivation and MACs.
void HMACSHA256(const unsigned char* key, unsigned keyLen,
                const unsigned char* data, unsigned dataLen,
                unsigned char mac[32]);

}
