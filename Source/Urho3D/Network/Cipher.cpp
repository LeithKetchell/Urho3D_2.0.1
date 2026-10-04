// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"
#include "Cipher.h"
#include "CryptoRNG.h"
#include "ChaCha20Poly1305.h"
#include "SHA256.h"
#include "X25519.h"
#include "../IO/Log.h"
#include <cstring>

#include "../DebugNew.h"

namespace Urho3D
{

Cipher::Cipher() :
    hasKeyPair_(false),
    ready_(false)
{
}

Cipher::~Cipher()
{
    // Securely zero key material
    if (secretKey_.Size())
        memset(secretKey_.Buffer(), 0, secretKey_.Size());
    if (txKey_.Size())
        memset(txKey_.Buffer(), 0, txKey_.Size());
    if (rxKey_.Size())
        memset(rxKey_.Buffer(), 0, rxKey_.Size());
}

bool Cipher::GenerateKeyPair()
{
    secretKey_.Resize(32);
    publicKey_.Resize(32);

    if (!X25519GenerateKey(secretKey_.Buffer()))
    {
        URHO3D_LOGERROR("Cipher: Failed to generate X25519 key pair");
        hasKeyPair_ = false;
        return false;
    }

    X25519PublicKey(publicKey_.Buffer(), secretKey_.Buffer());
    hasKeyPair_ = true;
    URHO3D_LOGINFO("Cipher: X25519 key pair generated");
    return true;
}

bool Cipher::DeriveSessionKeys(const unsigned char* remotePublicKey, unsigned remoteKeySize, bool isServer)
{
    if (!hasKeyPair_)
    {
        URHO3D_LOGERROR("Cipher: Cannot derive session keys — no key pair generated");
        return false;
    }

    if (remoteKeySize != 32)
    {
        URHO3D_LOGERROR("Cipher: Remote public key size mismatch (expected 32, got " +
            String(remoteKeySize) + ")");
        return false;
    }

    // Compute X25519 shared secret
    unsigned char sharedSecret[32];
    if (!X25519SharedSecret(sharedSecret, secretKey_.Buffer(), remotePublicKey))
    {
        URHO3D_LOGERROR("Cipher: X25519 shared secret computation failed (low-order point)");
        return false;
    }

    // Derive two 32-byte session keys from the shared secret using HKDF-like construction.
    // We use HMAC-SHA256 with the public keys as salt to ensure both sides derive the same
    // keys regardless of who is "server" vs "client".
    // Key material: HMAC-SHA256(sharedSecret, "server" || serverPub || clientPub || "tx")
    //               HMAC-SHA256(sharedSecret, "server" || serverPub || clientPub || "rx")
    // The server's tx is the client's rx and vice versa.

    const unsigned char* serverPub = isServer ? publicKey_.Buffer() : remotePublicKey;
    const unsigned char* clientPub = isServer ? remotePublicKey : publicKey_.Buffer();

    // Build info for key A: "urho3d-session-a" || serverPub || clientPub
    unsigned char infoA[16 + 32 + 32];
    memcpy(infoA, "urho3d-session-a", 16);
    memcpy(infoA + 16, serverPub, 32);
    memcpy(infoA + 48, clientPub, 32);

    unsigned char infoB[16 + 32 + 32];
    memcpy(infoB, "urho3d-session-b", 16);
    memcpy(infoB + 16, serverPub, 32);
    memcpy(infoB + 48, clientPub, 32);

    unsigned char keyA[32], keyB[32];
    HMACSHA256(sharedSecret, 32, infoA, sizeof(infoA), keyA);
    HMACSHA256(sharedSecret, 32, infoB, sizeof(infoB), keyB);

    // Zero the shared secret
    memset(sharedSecret, 0, 32);

    // Server: tx=A, rx=B. Client: tx=B, rx=A.
    txKey_.Resize(32);
    rxKey_.Resize(32);
    if (isServer)
    {
        memcpy(txKey_.Buffer(), keyA, 32);
        memcpy(rxKey_.Buffer(), keyB, 32);
    }
    else
    {
        memcpy(txKey_.Buffer(), keyB, 32);
        memcpy(rxKey_.Buffer(), keyA, 32);
    }

    memset(keyA, 0, 32);
    memset(keyB, 0, 32);

    ready_ = true;
    URHO3D_LOGINFO("Cipher: Session keys derived (" + String(isServer ? "server" : "client") + " side)");
    return true;
}

bool Cipher::Encrypt(const unsigned char* plaintext, unsigned plaintextSize, Vector<unsigned char>& ciphertext)
{
    if (!ready_)
    {
        URHO3D_LOGERROR("Cipher: Cannot encrypt — session keys not derived");
        return false;
    }

    // Output format: [12-byte nonce][ciphertext][16-byte tag]
    const unsigned nonceSize = 12;
    const unsigned tagSize = 16;
    ciphertext.Resize(nonceSize + plaintextSize + tagSize);

    // Generate random nonce
    unsigned char* nonce = ciphertext.Buffer();
    if (!CryptoRandomBytes(nonce, nonceSize))
    {
        URHO3D_LOGERROR("Cipher: Failed to generate nonce");
        return false;
    }

    unsigned char* ct = ciphertext.Buffer() + nonceSize;
    unsigned char* tag = ciphertext.Buffer() + nonceSize + plaintextSize;

    return ChaCha20Poly1305Seal(txKey_.Buffer(), nonce, plaintext, plaintextSize,
                                nullptr, 0, ct, tag);
}

bool Cipher::Decrypt(const unsigned char* ciphertext, unsigned ciphertextSize, Vector<unsigned char>& plaintext)
{
    if (!ready_)
    {
        URHO3D_LOGERROR("Cipher: Cannot decrypt — session keys not derived");
        return false;
    }

    const unsigned nonceSize = 12;
    const unsigned tagSize = 16;

    if (ciphertextSize < nonceSize + tagSize)
    {
        URHO3D_LOGERROR("Cipher: Ciphertext too short");
        return false;
    }

    const unsigned char* nonce = ciphertext;
    const unsigned char* ct = ciphertext + nonceSize;
    unsigned ctLen = ciphertextSize - nonceSize - tagSize;
    const unsigned char* tag = ciphertext + nonceSize + ctLen;

    plaintext.Resize(ctLen);

    if (!ChaCha20Poly1305Open(rxKey_.Buffer(), nonce, ct, ctLen,
                              nullptr, 0, tag, plaintext.Buffer()))
    {
        URHO3D_LOGERROR("Cipher: Decryption failed — authentication tag mismatch");
        return false;
    }

    return true;
}

bool Cipher::MixPasswordIntoKeys(const unsigned char* passwordHash, unsigned hashSize)
{
    if (!ready_ || !passwordHash || hashSize == 0)
    {
        URHO3D_LOGERROR("Cipher: Cannot mix password — keys not ready or invalid hash");
        return false;
    }

    // Derive new tx key: HMAC-SHA256(txKey, passwordHash)
    unsigned char newKey[32];
    HMACSHA256(txKey_.Buffer(), txKey_.Size(), passwordHash, hashSize, newKey);
    memcpy(txKey_.Buffer(), newKey, 32);

    // Derive new rx key: HMAC-SHA256(rxKey, passwordHash)
    HMACSHA256(rxKey_.Buffer(), rxKey_.Size(), passwordHash, hashSize, newKey);
    memcpy(rxKey_.Buffer(), newKey, 32);

    memset(newKey, 0, 32);

    URHO3D_LOGINFO("Cipher: Password mixed into session keys (PAKE)");
    return true;
}

}
