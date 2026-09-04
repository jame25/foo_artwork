#include "stdafx.h"
#include "crypto_utils.h"
#include <wincrypt.h>
#include <vector>
#include <string>

#pragma comment(lib, "crypt32.lib")

namespace crypto_utils {

static const char* kEncryptionPrefix = "ENC:";
static const size_t kPrefixLen = 4;

bool is_credential_encrypted(const char* str) {
    if (!str || strlen(str) < kPrefixLen) {
        return false;
    }
    return (strncmp(str, kEncryptionPrefix, kPrefixLen) == 0);
}

pfc::string8 encrypt_credential(const char* plaintext) {
    if (!plaintext || strlen(plaintext) == 0) {
        return "";
    }

    // If already encrypted, return as-is
    if (is_credential_encrypted(plaintext)) {
        return plaintext;
    }

    DATA_BLOB in_blob;
    in_blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext));
    in_blob.cbData = static_cast<DWORD>(strlen(plaintext));

    DATA_BLOB out_blob = { 0 };

    // Encrypt using Windows DPAPI tied to current logged-in user profile
    if (!CryptProtectData(
        &in_blob,
        L"foo_artwork credential",
        NULL,
        NULL,
        NULL,
        CRYPTPROTECT_UI_FORBIDDEN,
        &out_blob)) {
        return "";
    }

    // Convert encrypted binary blob to Base64 ASCII string
    DWORD b64_len = 0;
    if (!CryptBinaryToStringA(
        out_blob.pbData,
        out_blob.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        NULL,
        &b64_len) || b64_len == 0) {
        LocalFree(out_blob.pbData);
        return "";
    }

    std::string b64_str(b64_len, '\0');
    if (!CryptBinaryToStringA(
        out_blob.pbData,
        out_blob.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        &b64_str[0],
        &b64_len)) {
        LocalFree(out_blob.pbData);
        return "";
    }

    LocalFree(out_blob.pbData);

    // Trim any trailing nulls or newlines
    while (!b64_str.empty() && (b64_str.back() == '\0' || b64_str.back() == '\r' || b64_str.back() == '\n')) {
        b64_str.pop_back();
    }

    pfc::string8 result = kEncryptionPrefix;
    result << b64_str.c_str();
    return result;
}

pfc::string8 decrypt_credential(const char* ciphertext_or_plaintext) {
    if (!ciphertext_or_plaintext || strlen(ciphertext_or_plaintext) == 0) {
        return "";
    }

    // If not starting with "ENC:", it is legacy plain text; return as-is for backward-compatibility
    if (!is_credential_encrypted(ciphertext_or_plaintext)) {
        return ciphertext_or_plaintext;
    }

    const char* b64_ptr = ciphertext_or_plaintext + kPrefixLen;
    DWORD b64_len = static_cast<DWORD>(strlen(b64_ptr));
    if (b64_len == 0) {
        return "";
    }

    // Decode Base64 to binary ciphertext
    DWORD binary_size = 0;
    if (!CryptStringToBinaryA(
        b64_ptr,
        b64_len,
        CRYPT_STRING_BASE64,
        NULL,
        &binary_size,
        NULL,
        NULL) || binary_size == 0) {
        return "";
    }

    std::vector<BYTE> cipher_bytes(binary_size);
    if (!CryptStringToBinaryA(
        b64_ptr,
        b64_len,
        CRYPT_STRING_BASE64,
        cipher_bytes.data(),
        &binary_size,
        NULL,
        NULL)) {
        return "";
    }

    DATA_BLOB in_blob;
    in_blob.pbData = cipher_bytes.data();
    in_blob.cbData = binary_size;

    DATA_BLOB out_blob = { 0 };

    // Decrypt using Windows DPAPI
    if (!CryptUnprotectData(
        &in_blob,
        NULL,
        NULL,
        NULL,
        NULL,
        CRYPTPROTECT_UI_FORBIDDEN,
        &out_blob)) {
        // Decryption failed (e.g. copied to a different machine or user profile)
        return "";
    }

    pfc::string8 plaintext(reinterpret_cast<const char*>(out_blob.pbData), out_blob.cbData);

    // Securely wipe memory buffer
    SecureZeroMemory(out_blob.pbData, out_blob.cbData);
    LocalFree(out_blob.pbData);

    return plaintext;
}

} // namespace crypto_utils
