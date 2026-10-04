// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// Hand-rolled X25519 Diffie-Hellman key exchange — RFC 7748, no third-party library.

#pragma once

namespace Urho3D
{

/// Generate a random X25519 secret key (32 bytes) with proper clamping.
/// Returns true on success.
bool X25519GenerateKey(unsigned char secretKey[32]);

/// Compute X25519 public key from secret key.
/// publicKey = secretKey * basepoint(9).
void X25519PublicKey(unsigned char publicKey[32], const unsigned char secretKey[32]);

/// Compute X25519 shared secret: result = secretKey * peerPublicKey.
/// Returns true on success. False if the result is all zeros (low-order point).
bool X25519SharedSecret(unsigned char sharedSecret[32],
                        const unsigned char secretKey[32],
                        const unsigned char peerPublicKey[32]);

}
