#include "patch.h"
#include <bcrypt.h>
#include <string.h>

static const unsigned char original_checksum[16] = {
    0x0e,0xe3,0x78,0xa9,0x37,0xd4,0xb0,0xaf,0xf9,0x6a,0xa1,0x31,0x7f,0x12,0x43,0x3a
};
static const unsigned char corrected_checksum[16] = {
    0x99,0xc0,0x9a,0xe6,0x6c,0x92,0xa6,0xff,0x58,0xb0,0x86,0xd9,0x02,0xbc,0xeb,0x5d
};
static const unsigned char original_sha256[32] = {
    0x62,0xc6,0x1e,0x70,0x60,0x1a,0xf0,0xee,0x30,0xa8,0x11,0xcf,0xfb,0xb6,0x6f,0x55,
    0xcd,0x8e,0xda,0x6e,0x0c,0x88,0xb6,0x9c,0xbf,0xbd,0x64,0x63,0x70,0x23,0x71,0x62
};
static const unsigned char corrected_sha256[32] = {
    0x3b,0xbd,0x8c,0x85,0x8d,0xc3,0x1d,0x5d,0xfd,0x45,0x54,0x86,0x07,0x10,0x11,0x30,
    0x8e,0x35,0xa0,0x20,0x16,0xb5,0xc4,0xeb,0x40,0x5a,0xfa,0x0b,0xec,0x2c,0xee,0xd6
};

static HRESULT sha256(const unsigned char *data, ULONG size, unsigned char digest[32])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) return HRESULT_FROM_NT(status);
    status = BCryptHash(algorithm, NULL, 0, (PUCHAR)data, size, digest, 32);
    NTSTATUS close_status = BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return HRESULT_FROM_NT(status);
    return close_status < 0 ? HRESULT_FROM_NT(close_status) : S_OK;
}

/* Swap the two outputs of the exact wave-impulse pixel shader. The draw hook
 * uses this variant only for the second, separate RT0 draw. No game bytecode
 * is bundled: both input and transformed output must match full SHA256s. */
HRESULT patch_water_shader(const void *data, SIZE_T size, unsigned char **output)
{
    static const unsigned char input_hash[32] = {
        0x94,0x46,0x8a,0xd4,0x14,0x66,0x0d,0x11,0x6e,0xa3,0xb4,0xc9,0x6a,0xb7,0xa3,0x01,
        0x87,0x96,0xce,0x8b,0x33,0xe8,0xbd,0x14,0x27,0xc8,0x68,0x00,0x25,0x4e,0x9a,0xc5
    };
    static const unsigned char output_hash[32] = {
        0x2f,0x7f,0x4b,0x48,0x47,0x94,0x5d,0x62,0x8b,0x2d,0x5c,0xed,0x8d,0x0e,0xdb,0x61,
        0xf2,0x09,0x19,0xe8,0x5c,0xd9,0xb5,0x05,0x04,0xd1,0x0f,0x8a,0xbc,0x45,0xca,0xca
    };
    static const unsigned char checksum[16] = {
        0xc7,0x11,0x0a,0xcf,0xe7,0x83,0x6b,0x12,0x57,0xfb,0x27,0xf5,0x7f,0xf4,0xb6,0x93
    };
    *output = NULL;
    if (!data || size != 1924 || memcmp(data, "DXBC", 4)) return S_FALSE;
    unsigned char digest[32];
    HRESULT hr = sha256(data, (ULONG)size, digest);
    if (FAILED(hr)) return hr;
    if (memcmp(digest, input_hash, 32)) return S_FALSE;
    unsigned char *bytes = HeapAlloc(GetProcessHeap(), 0, size);
    if (!bytes) return E_OUTOFMEMORY;
    memcpy(bytes, data, size);
    const unsigned offsets[] = {1028,1040,1336,1492,1552,1704,1732};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        DWORD reg;
        memcpy(&reg, bytes + offsets[i] + 8, 4);
        reg = 1 - reg;
        memcpy(bytes + offsets[i] + 8, &reg, 4);
    }
    for (DWORD i = 0; i < 2; ++i) {
        DWORD reg = 1 - i;
        memcpy(bytes + 920 + i * 24 + 4, &reg, 4);
        memcpy(bytes + 920 + i * 24 + 16, &reg, 4);
    }
    unsigned char temporary[24];
    memcpy(temporary, bytes + 920, 24);
    memcpy(bytes + 920, bytes + 944, 24);
    memcpy(bytes + 944, temporary, 24);
    memcpy(temporary, bytes + 1028, 12);
    memcpy(bytes + 1028, bytes + 1040, 12);
    memcpy(bytes + 1040, temporary, 12);
    memcpy(bytes + 4, checksum, 16);
    hr = sha256(bytes, (ULONG)size, digest);
    if (SUCCEEDED(hr) && memcmp(digest, output_hash, 32)) hr = E_UNEXPECTED;
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, bytes); return hr; }
    *output = bytes;
    return S_OK;
}

HRESULT patch_cloud_shader(const void *data, SIZE_T size, unsigned char **output)
{
    *output = NULL;
    const unsigned char *source = data;
    if (!source || size < 20 || memcmp(source, "DXBC", 4) ||
        memcmp(source + 4, original_checksum, 16))
        return S_FALSE;
    if (size != 11904) return E_INVALIDARG;
    unsigned char digest[32];
    HRESULT hr = sha256(source, (ULONG)size, digest);
    if (FAILED(hr)) return hr;
    if (memcmp(digest, original_sha256, 32)) return E_INVALIDARG;
    DWORD before, middle, after;
    memcpy(&before, source + 11316, 4);
    memcpy(&middle, source + 11576, 4);
    memcpy(&after, source + 11612, 4);
    if (before != 0x010018be || middle != 0x010010be || after != 0x010018be)
        return E_INVALIDARG;
    unsigned char *patched = HeapAlloc(GetProcessHeap(), 0, size);
    if (!patched) return E_OUTOFMEMORY;
    memcpy(patched, source, size);
    /* Exact input fingerprint makes the precomputed container checksum valid. */
    middle |= 0x800;
    memcpy(patched + 11576, &middle, 4);
    memcpy(patched + 4, corrected_checksum, 16);
    hr = sha256(patched, (ULONG)size, digest);
    if (SUCCEEDED(hr) && memcmp(digest, corrected_sha256, 32)) hr = E_UNEXPECTED;
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, patched);
        return hr;
    }
    *output = patched;
    return S_OK;
}
