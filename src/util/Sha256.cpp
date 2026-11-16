// SHA-256 con CNG (bcrypt.dll). Ver Sha256.h.
#include "util/Sha256.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>

namespace cola::util {

std::wstring Sha256Hex(std::string_view data) {
    // Proveedor del algoritmo -> objeto hash -> datos -> resultado (32 bytes).
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return L"";
    std::wstring hex;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        bool ok = true;
        // BCryptHashData recibe el largo en un ULONG: se pasa por tramos para
        // aceptar búferes de más de 4 GB (no se dan, pero así no hay truncamiento).
        size_t offset = 0;
        while (ok && offset < data.size()) {
            ULONG chunk = static_cast<ULONG>(std::min<size_t>(data.size() - offset, 0x40000000u));
            ok = BCRYPT_SUCCESS(BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data() + offset)), chunk, 0));
            offset += chunk;
        }
        UCHAR digest[32] = {};
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static constexpr wchar_t kDigits[] = L"0123456789abcdef";
            for (UCHAR byte : digest) {
                hex.push_back(kDigits[byte >> 4]);
                hex.push_back(kDigits[byte & 15]);
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

}  // namespace cola::util
