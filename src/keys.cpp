// Key derivations from zmod_nfc's decoders/vendors.py (adapted there from
// OpenRFID, GPL-3.0; keys from the Bambu Research Group RFID guide, the
// Creality CFS RFID research and DnG-Crafts U1-RFID).
#include "keys.h"

#include <mbedtls/aes.h>
#include <mbedtls/md.h>

const uint8_t DEFAULT_KEYS[4][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},   // factory default
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},   // MAD sector key A
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7},   // NDEF sector key A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};
const char *const DEFAULT_KEYSET_NAME = "default";

namespace {

const uint8_t BAMBU_KEY[16] = {0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff,
                               0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96};
const char CREALITY_KEY[] = "q3bu^t1nqfZ(pf$1";
const char SNAPMAKER_KEY[] = "Snapmaker_qwertyuiop[,.;]_1q2w3e";

void hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t len, uint8_t out[32]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLen, data, len, out);
}

void bambu(const uint8_t *uid, KeySet &set) {
    set.name = "Bambu";
    uint8_t okm[96];
    const uint8_t info[] = {'R', 'F', 'I', 'D', '-', 'A', 0};
    hkdfSha256(uid, 4, BAMBU_KEY, sizeof(BAMBU_KEY), info, sizeof(info), okm, sizeof(okm));
    for (int s = 0; s < KEYSET_SECTORS; s++) {
        memcpy(set.a[s], okm + s * 6, 6);
        set.hasA[s] = true;
        set.hasB[s] = false;
    }
}

// Key A from the first 25 bytes of the salt, key B from all 32.
void snapmaker(const uint8_t *uid, KeySet &set) {
    set.name = "Snapmaker";
    const uint8_t *salt = (const uint8_t *)SNAPMAKER_KEY;
    char info[16];
    for (int s = 0; s < KEYSET_SECTORS; s++) {
        int n = snprintf(info, sizeof(info), "key_a_%d", s);
        hkdfSha256(uid, 4, salt, 25, (const uint8_t *)info, n, set.a[s], 6);
        n = snprintf(info, sizeof(info), "key_b_%d", s);
        hkdfSha256(uid, 4, salt, 32, (const uint8_t *)info, n, set.b[s], 6);
        set.hasA[s] = set.hasB[s] = true;
    }
}

// Sector 1's key A is the UID (4 bytes, repeated) AES-encrypted; every
// other key is the factory default.
void creality(const uint8_t *uid, KeySet &set) {
    set.name = "Creality";
    uint8_t block[16], derived[16];
    for (int i = 0; i < 4; i++) memcpy(block + i * 4, uid, 4);
    aes128Ecb((const uint8_t *)CREALITY_KEY, block, derived, 16, false);
    for (int s = 0; s < KEYSET_SECTORS; s++) {
        memcpy(set.a[s], s == 1 ? derived : DEFAULT_KEYS[0], 6);
        memcpy(set.b[s], DEFAULT_KEYS[0], 6);
        set.hasA[s] = set.hasB[s] = true;
    }
}

}  // namespace

void hkdfSha256(const uint8_t *ikm, size_t ikmLen, const uint8_t *salt, size_t saltLen,
                const uint8_t *info, size_t infoLen, uint8_t *out, size_t outLen) {
    uint8_t zeros[32] = {};
    uint8_t prk[32];
    if (!salt || !saltLen) {
        salt = zeros;
        saltLen = sizeof(zeros);
    }
    hmacSha256(salt, saltLen, ikm, ikmLen, prk);
    uint8_t block[32];
    uint8_t input[32 + 64 + 1];
    size_t blockLen = 0, done = 0;
    for (uint8_t counter = 1; done < outLen; counter++) {
        size_t pos = 0;
        memcpy(input, block, blockLen);
        pos += blockLen;
        size_t take = min(infoLen, (size_t)64);
        memcpy(input + pos, info, take);
        pos += take;
        input[pos++] = counter;
        hmacSha256(prk, sizeof(prk), input, pos, block);
        blockLen = 32;
        size_t n = min((size_t)32, outLen - done);
        memcpy(out + done, block, n);
        done += n;
    }
}

void aes128Ecb(const uint8_t key[16], const uint8_t *in, uint8_t *out, size_t len, bool decrypt) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (decrypt)
        mbedtls_aes_setkey_dec(&ctx, key, 128);
    else
        mbedtls_aes_setkey_enc(&ctx, key, 128);
    for (size_t i = 0; i + 16 <= len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, decrypt ? MBEDTLS_AES_DECRYPT : MBEDTLS_AES_ENCRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

void vendorKeysets(const uint8_t *uid, KeySet out[VENDOR_KEYSETS]) {
    bambu(uid, out[0]);
    snapmaker(uid, out[1]);
    creality(uid, out[2]);
}
