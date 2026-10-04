// Copyright (c) 2008-2022 the Urho3D project
// License: MIT
// Hand-rolled cryptographically secure RNG — no third-party library.

#pragma once

namespace Urho3D
{

/// Fill buffer with cryptographically secure random bytes.
/// Uses /dev/urandom on Unix, CryptGenRandom on Windows.
/// Returns true on success.
bool CryptoRandomBytes(unsigned char* buffer, unsigned size);

}
