// MIFARE Classic keys per tag family, derived from the tag's UID like
// FF_NFC's decoders do (Bambu Lab, Snapmaker, Creality). The family names
// are sent to the printer plugin, whose decoders check them.
#pragma once

#include <Arduino.h>

constexpr int KEYSET_SECTORS = 16;

struct KeySet {
    const char *name;
    uint8_t a[KEYSET_SECTORS][6];
    uint8_t b[KEYSET_SECTORS][6];
    bool hasA[KEYSET_SECTORS];
    bool hasB[KEYSET_SECTORS];
};

constexpr int VENDOR_KEYSETS = 3;
// Bambu, Snapmaker, Creality - for the first 4 bytes of the UID.
void vendorKeysets(const uint8_t *uid, KeySet out[VENDOR_KEYSETS]);

// Keys that open most blank or NDEF-formatted tags.
extern const uint8_t DEFAULT_KEYS[4][6];
extern const char *const DEFAULT_KEYSET_NAME;

void hkdfSha256(const uint8_t *ikm, size_t ikmLen, const uint8_t *salt, size_t saltLen,
                const uint8_t *info, size_t infoLen, uint8_t *out, size_t outLen);
void aes128Ecb(const uint8_t key[16], const uint8_t *in, uint8_t *out, size_t len, bool decrypt);
