// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"
#include "CryptoRNG.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Urho3D
{

bool CryptoRandomBytes(unsigned char* buffer, unsigned size)
{
    if (!buffer || size == 0)
        return false;

#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        return false;
    BOOL ok = CryptGenRandom(hProv, size, buffer);
    CryptReleaseContext(hProv, 0);
    return ok != FALSE;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;
    unsigned totalRead = 0;
    while (totalRead < size)
    {
        ssize_t n = read(fd, buffer + totalRead, size - totalRead);
        if (n <= 0)
        {
            close(fd);
            return false;
        }
        totalRead += (unsigned)n;
    }
    close(fd);
    return true;
#endif
}

}
