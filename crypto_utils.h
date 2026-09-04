#pragma once
#include "stdafx.h"
#include <wincrypt.h>

namespace crypto_utils {

// Encrypts a plaintext credential using Windows DPAPI (CryptProtectData).
// Returns an ASCII string formatted as "ENC:<base64-ciphertext>".
// If plaintext is empty or null, returns empty string.
// If plaintext is already encrypted (starts with "ENC:"), returns it unmodified.
pfc::string8 encrypt_credential(const char* plaintext);

// Decrypts a credential using Windows DPAPI (CryptUnprotectData).
// If input starts with "ENC:", it decodes the Base64 ciphertext and decrypts with DPAPI.
// If input does not start with "ENC:", it is treated as legacy unencrypted plaintext and returned as-is.
// If decryption fails (e.g. key from a different user/machine), returns empty string.
pfc::string8 decrypt_credential(const char* ciphertext_or_plaintext);

// Checks if a credential string is in encrypted format ("ENC:...").
bool is_credential_encrypted(const char* str);

} // namespace crypto_utils
