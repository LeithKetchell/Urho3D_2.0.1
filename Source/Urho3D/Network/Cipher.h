// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// Network cipher using hand-rolled X25519 + ChaCha20-Poly1305 + SHA-256.
// No third-party crypto library required.

#pragma once

#include "../Container/RefCounted.h"
#include "../Container/Vector.h"

namespace Urho3D
{

/// Network cipher: X25519 key exchange, ChaCha20-Poly1305 AEAD encryption,
/// SHA-256/HMAC for key derivation. All crypto is hand-rolled — zero dependencies.
class URHO3D_API Cipher : public RefCounted
{
public:
    Cipher();
    ~Cipher();

    /// Generate X25519 key pair. Returns true on success.
    bool GenerateKeyPair();
    /// Return the 32-byte public key.
    const Vector<unsigned char>& GetPublicKey() const { return publicKey_; }
    /// Return public key size (32 bytes).
    unsigned GetPublicKeySize() const { return 32; }
    /// Derive tx/rx session keys via X25519 shared secret + HKDF-SHA256.
    /// isServer determines which half of the derived key material is tx vs rx.
    bool DeriveSessionKeys(const unsigned char* remotePublicKey, unsigned remoteKeySize, bool isServer);
    /// Return true if session keys have been derived and encryption is ready.
    bool IsReady() const { return ready_; }
    /// Encrypt plaintext using ChaCha20-Poly1305 AEAD.
    /// Output format: [12-byte nonce][ciphertext][16-byte tag].
    bool Encrypt(const unsigned char* plaintext, unsigned plaintextSize, Vector<unsigned char>& ciphertext);
    /// Decrypt ciphertext using ChaCha20-Poly1305 AEAD.
    /// Expects format: [12-byte nonce][ciphertext][16-byte tag].
    bool Decrypt(const unsigned char* ciphertext, unsigned ciphertextSize, Vector<unsigned char>& plaintext);
    /// Return overhead: 12-byte nonce + 16-byte tag = 28 bytes.
    unsigned GetOverhead() const { return 28; }
    /// Return cipher name.
    const char* GetName() const { return "X25519-ChaCha20-Poly1305"; }
    /// Mix password hash into session keys using HMAC-SHA256(key, passwordHash).
    /// Used for PAKE authentication.
    bool MixPasswordIntoKeys(const unsigned char* passwordHash, unsigned hashSize);

private:
    Vector<unsigned char> publicKey_;   ///< Our X25519 public key (32 bytes)
    Vector<unsigned char> secretKey_;   ///< Our X25519 secret key (32 bytes)
    Vector<unsigned char> txKey_;       ///< Session transmit key (32 bytes)
    Vector<unsigned char> rxKey_;       ///< Session receive key (32 bytes)
    bool hasKeyPair_;
    bool ready_;
};

}
