// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// Hand-rolled ChaCha20-Poly1305 AEAD — RFC 8439 compliant, no third-party library.

#pragma once

namespace Urho3D
{

/// ChaCha20 stream cipher. Operates on a 256-bit key and 96-bit nonce.
/// Per RFC 8439 (IETF variant: 32-bit counter + 96-bit nonce).
class ChaCha20
{
public:
    /// Initialize with 32-byte key, 12-byte nonce, and initial counter value.
    void Init(const unsigned char key[32], const unsigned char nonce[12], unsigned counter = 0);
    /// Encrypt/decrypt (XOR with keystream). In-place is fine (out == in).
    void Process(const unsigned char* in, unsigned char* out, unsigned len);
    /// Generate keystream block for Poly1305 key derivation (counter=0).
    void KeystreamBlock(unsigned char block[64]);

private:
    unsigned state_[16];
    unsigned counter_;
};

/// Poly1305 one-time authenticator. 32-byte one-time key, produces 16-byte tag.
/// Per RFC 8439 Section 2.5.
class Poly1305
{
public:
    /// Initialize with 32-byte one-time key.
    void Init(const unsigned char key[32]);
    /// Feed data.
    void Update(const unsigned char* data, unsigned len);
    /// Finalize and produce 16-byte tag.
    void Final(unsigned char tag[16]);

private:
    void ProcessBlock(const unsigned char* block, unsigned len, bool isFinal);

    unsigned r_[5];       // Clamped key r (base 2^26 limbs)
    unsigned h_[5];       // Accumulator
    unsigned pad_[4];     // Key s (second 16 bytes)
};

/// ChaCha20-Poly1305 AEAD — RFC 8439 construction.
/// Encrypt: plaintext → nonce + ciphertext + 16-byte tag.
/// Decrypt: verify tag, then decrypt.

/// Encrypt with AEAD. Output: ciphertext (same size as plaintext).
/// Tag is written to tag[16]. Nonce must be 12 bytes, unique per message.
/// aad/aadLen: additional authenticated data (can be null/0).
bool ChaCha20Poly1305Seal(
    const unsigned char key[32],
    const unsigned char nonce[12],
    const unsigned char* plaintext, unsigned plaintextLen,
    const unsigned char* aad, unsigned aadLen,
    unsigned char* ciphertext,
    unsigned char tag[16]);

/// Decrypt with AEAD. Returns true if tag is valid.
/// plaintext output is same size as ciphertext.
bool ChaCha20Poly1305Open(
    const unsigned char key[32],
    const unsigned char nonce[12],
    const unsigned char* ciphertext, unsigned ciphertextLen,
    const unsigned char* aad, unsigned aadLen,
    const unsigned char tag[16],
    unsigned char* plaintext);

}
