#include "stdafx.h"
#include "acrcloud_client.h"
#include "nlohmann/json.hpp"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

using json = nlohmann::json;

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static std::string base64_encode(const uint8_t* buf, size_t bufLen) {
    std::string ret;
    int i = 0, j = 0;
    uint8_t char_array_3[3], char_array_4[4];

    while (bufLen--) {
        char_array_3[i++] = *(buf++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        for (j = 0; j < i + 1; j++) ret += base64_chars[char_array_4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

static std::vector<uint8_t> hmac_sha1_win32(const std::string& key, const std::string& data) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    std::vector<uint8_t> result;

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        struct KeyBlob {
            BLOBHEADER header;
            DWORD len;
            BYTE key[1024];
        } blob;
        memset(&blob, 0, sizeof(blob));
        blob.header.bType = PLAINTEXTKEYBLOB;
        blob.header.bVersion = CUR_BLOB_VERSION;
        blob.header.reserved = 0;
        blob.header.aiKeyAlg = CALG_RC2;
        blob.len = (DWORD)(key.length() < 1024 ? key.length() : 1024);
        memcpy(blob.key, key.data(), blob.len);

        if (CryptImportKey(hProv, (BYTE*)&blob, sizeof(BLOBHEADER) + sizeof(DWORD) + blob.len, 0, CRYPT_IPSEC_HMAC_KEY, &hKey)) {
            HMAC_INFO hmacInfo = {0};
            hmacInfo.HashAlgid = CALG_SHA1;
            if (CryptCreateHash(hProv, CALG_HMAC, hKey, 0, &hHash)) {
                CryptSetHashParam(hHash, HP_HMAC_INFO, (BYTE*)&hmacInfo, 0);
                CryptHashData(hHash, (const BYTE*)data.data(), (DWORD)data.length(), 0);
                DWORD hashLen = 20;
                result.resize(hashLen);
                CryptGetHashParam(hHash, HP_HASHVAL, result.data(), &hashLen, 0);
                CryptDestroyHash(hHash);
            }
            CryptDestroyKey(hKey);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
}

std::string ACRCloudClient::generate_signature(
    const std::string& method,
    const std::string& uri,
    const std::string& access_key,
    const std::string& access_secret,
    const std::string& data_type,
    const std::string& signature_version,
    long timestamp) {

    std::string sign_string = method + "\n" +
                              uri + "\n" +
                              access_key + "\n" +
                              data_type + "\n" +
                              signature_version + "\n" +
                              std::to_string(timestamp);

    std::vector<uint8_t> hmac_bytes = hmac_sha1_win32(access_secret, sign_string);
    return base64_encode(hmac_bytes.data(), hmac_bytes.size());
}

ACRCloudClient::RecognitionResult ACRCloudClient::parse_response_json(const std::string& json_response) {
    RecognitionResult res;
    try {
        auto j = json::parse(json_response);
        if (j.contains("status") && j["status"].contains("code")) {
            int code = j["status"]["code"].get<int>();
            std::string msg = j["status"].contains("msg") ? j["status"]["msg"].get<std::string>() : "Unknown";

            res.status_code = code;
            foo_artwork::log_printf("foo_artwork: ACRCloud Response Status Code: %d (%s)", code, msg.c_str());

            if (code == 0 && j.contains("metadata") && j["metadata"].contains("music")) {
                auto& music_list = j["metadata"]["music"];
                if (music_list.is_array() && !music_list.empty()) {
                    auto& track_obj = music_list[0];
                    if (track_obj.contains("title")) {
                        res.title = track_obj["title"].get<std::string>();
                    }
                    if (track_obj.contains("artists") && track_obj["artists"].is_array() && !track_obj["artists"].empty()) {
                        if (track_obj["artists"][0].contains("name")) {
                            res.artist = track_obj["artists"][0]["name"].get<std::string>();
                        }
                    }
                    if (track_obj.contains("album") && track_obj["album"].contains("name")) {
                        res.album = track_obj["album"]["name"].get<std::string>();
                    }
                    if (track_obj.contains("duration_ms")) {
                        res.duration_ms = track_obj["duration_ms"].get<uint32_t>();
                    }
                    if (track_obj.contains("play_offset_ms")) {
                        res.play_offset_ms = track_obj["play_offset_ms"].get<uint32_t>();
                    }
                    res.success = !res.artist.empty() && !res.title.empty();
                    if (res.success) {
                        foo_artwork::log_printf("foo_artwork: ACRCloud Recognized Track: '%s - %s'", res.artist.c_str(), res.title.c_str());
                    }
                }
            } else {
                res.error_message = msg;
            }
        }
    } catch (const std::exception& e) {
        res.error_message = e.what();
        foo_artwork::log_printf("foo_artwork: ACRCloud JSON Parse Error: %s", e.what());
    }
    return res;
}

bool ACRCloudClient::create_fingerprint(
    const int16_t* pcm_samples,
    size_t sample_count,
    int sample_rate,
    std::vector<uint8_t>& out_fp) {

    out_fp.clear();

    const uint8_t magic[4] = { 'A', 'C', 'R', '1' };
    out_fp.insert(out_fp.end(), magic, magic + 4);

    uint16_t version = 1;
    uint16_t channels = 1;
    uint32_t rate = (uint32_t)(sample_rate > 0 ? sample_rate : 16000);
    uint32_t count = (uint32_t)sample_count;

    auto push_u16 = [&](uint16_t v) {
        out_fp.push_back((uint8_t)(v & 0xFF));
        out_fp.push_back((uint8_t)((v >> 8) & 0xFF));
    };
    auto push_u32 = [&](uint32_t v) {
        out_fp.push_back((uint8_t)(v & 0xFF));
        out_fp.push_back((uint8_t)((v >> 8) & 0xFF));
        out_fp.push_back((uint8_t)((v >> 16) & 0xFF));
        out_fp.push_back((uint8_t)((v >> 24) & 0xFF));
    };

    push_u16(version);
    push_u16(channels);
    push_u32(rate);
    push_u32(count);

    if (pcm_samples && sample_count > 0) {
        size_t frame_size = 256;
        size_t step = 128;
        size_t frame_count = (sample_count >= frame_size) ? (sample_count - frame_size) / step : 0;

        for (size_t f = 0; f < frame_count; ++f) {
            size_t offset = f * step;
            int32_t energy = 0;
            for (size_t i = 0; i < frame_size; ++i) {
                int16_t s = pcm_samples[offset + i];
                energy += (s * s) >> 16;
            }
            uint16_t hash = (uint16_t)((energy ^ (offset & 0xFFFF)) & 0xFFFF);
            push_u16(hash);
        }
    }

    foo_artwork::log_printf("foo_artwork: Local Fingerprint generated (%u bytes)", (unsigned int)out_fp.size());
    return !out_fp.empty();
}

ACRCloudClient::RecognitionResult ACRCloudClient::recognize_fingerprint(
    const char* host,
    const char* access_key,
    const char* access_secret,
    const uint8_t* fp_data,
    size_t fp_size) {

    RecognitionResult res;
    if (!host || strlen(host) == 0 || !access_key || strlen(access_key) == 0 || !access_secret || strlen(access_secret) == 0) {
        res.error_message = "ACRCloud credentials missing or unconfigured";
        return res;
    }

    long ts = (long)time(nullptr);
    std::string sig = generate_signature("POST", "/v1/identify", access_key, access_secret, "fingerprint", "1", ts);

    std::string host_str(host);
    if (host_str.find("http://") != 0 && host_str.find("https://") != 0) {
        host_str = "https://" + host_str;
    }
    std::string url = host_str + "/v1/identify";

    foo_artwork::log_printf("foo_artwork: ACRCloud Fingerprint Recognition initiated (Host: %s, FP size: %u bytes)",
                   host_str.c_str(), (unsigned int)fp_size);

    std::string boundary = "----ACRCloudBoundary123456";
    std::string body;

    auto add_field = [&](const std::string& name, const std::string& value) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
        body += value + "\r\n";
    };

    add_field("access_key", access_key);
    add_field("data_type", "fingerprint");
    add_field("signature_version", "1");
    add_field("signature", sig);
    add_field("timestamp", std::to_string(ts));
    add_field("sample_bytes", std::to_string(fp_size));

    // Attach local compact fingerprint buffer
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"sample\"; filename=\"fingerprint.fp\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    if (fp_data && fp_size > 0) {
        body.append((const char*)fp_data, fp_size);
    }
    body += "\r\n--" + boundary + "--\r\n";

    // WinHTTP POST
    std::wstring wide_url(url.begin(), url.end());
    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
        res.error_message = "Failed to parse ACRCloud URL: " + url;
        return res;
    }

    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring object(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.6", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        res.error_message = "WinHttpOpen failed";
        return res;
    }

    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpConnect failed";
        return res;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", object.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpOpenRequest failed";
        return res;
    }

    std::wstring content_type_header = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end());
    WinHttpAddRequestHeaders(hRequest, content_type_header.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpSendRequest failed with error " + std::to_string(err);
        return res;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpReceiveResponse failed with error " + std::to_string(err);
        return res;
    }

    std::string response_text;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        std::vector<char> buf(dwSize + 1);
        if (!WinHttpReadData(hRequest, buf.data(), dwSize, &dwDownloaded)) break;
        buf[dwDownloaded] = 0;
        response_text.append(buf.data(), dwDownloaded);
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    foo_artwork::log_printf("foo_artwork: ACRCloud Response Received (%u bytes)", (unsigned int)response_text.size());

    return parse_response_json(response_text);
}

static bool build_wav_file(const int16_t* pcm_samples, size_t sample_count, int sample_rate, std::vector<uint8_t>& out_wav) {
    out_wav.clear();
    if (!pcm_samples || sample_count == 0) return false;

    uint32_t data_size = (uint32_t)(sample_count * sizeof(int16_t));
    uint32_t riff_size = 36 + data_size;
    uint32_t rate = (sample_rate > 0) ? (uint32_t)sample_rate : 16000;
    uint16_t channels = 1;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = rate * channels * (bits_per_sample / 8);
    uint16_t block_align = channels * (bits_per_sample / 8);

    out_wav.resize(44 + data_size);
    uint8_t* p = out_wav.data();

    // RIFF header
    memcpy(p, "RIFF", 4); p += 4;
    memcpy(p, &riff_size, 4); p += 4;
    memcpy(p, "WAVE", 4); p += 4;

    // fmt subchunk
    memcpy(p, "fmt ", 4); p += 4;
    uint32_t fmt_size = 16;
    memcpy(p, &fmt_size, 4); p += 4;
    uint16_t audio_format = 1; // PCM
    memcpy(p, &audio_format, 2); p += 2;
    memcpy(p, &channels, 2); p += 2;
    memcpy(p, &rate, 4); p += 4;
    memcpy(p, &byte_rate, 4); p += 4;
    memcpy(p, &block_align, 2); p += 2;
    memcpy(p, &bits_per_sample, 2); p += 2;

    // data subchunk
    memcpy(p, "data", 4); p += 4;
    memcpy(p, &data_size, 4); p += 4;

    // PCM samples
    memcpy(p, pcm_samples, data_size);

    return true;
}

ACRCloudClient::RecognitionResult ACRCloudClient::recognize_audio(
    const char* host,
    const char* access_key,
    const char* access_secret,
    const uint8_t* audio_data,
    size_t audio_size) {

    RecognitionResult res;
    if (!host || strlen(host) == 0 || !access_key || strlen(access_key) == 0 || !access_secret || strlen(access_secret) == 0) {
        res.error_message = "ACRCloud credentials missing or unconfigured";
        return res;
    }

    std::vector<uint8_t> wav_payload;
    if (audio_data && audio_size >= sizeof(int16_t)) {
        const int16_t* pcm_samples = reinterpret_cast<const int16_t*>(audio_data);
        size_t sample_count = audio_size / sizeof(int16_t);
        build_wav_file(pcm_samples, sample_count, 16000, wav_payload);
    }

    if (wav_payload.empty()) {
        res.error_message = "Empty audio PCM payload for ACRCloud recognition";
        return res;
    }

    long ts = (long)time(nullptr);
    std::string sig = generate_signature("POST", "/v1/identify", access_key, access_secret, "audio", "1", ts);

    std::string host_str(host);
    if (host_str.find("http://") != 0 && host_str.find("https://") != 0) {
        host_str = "https://" + host_str;
    }
    std::string url = host_str + "/v1/identify";

    foo_artwork::log_printf("foo_artwork: ACRCloud Audio Snippet Recognition initiated (Host: %s, WAV size: %u bytes)",
                   host_str.c_str(), (unsigned int)wav_payload.size());

    std::string boundary = "----ACRCloudBoundary123456";
    std::string body;

    auto add_field = [&](const std::string& name, const std::string& value) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
        body += value + "\r\n";
    };

    add_field("access_key", access_key);
    add_field("data_type", "audio");
    add_field("signature_version", "1");
    add_field("signature", sig);
    add_field("timestamp", std::to_string(ts));
    add_field("sample_bytes", std::to_string(wav_payload.size()));

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"sample\"; filename=\"sample.wav\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body.append((const char*)wav_payload.data(), wav_payload.size());
    body += "\r\n--" + boundary + "--\r\n";

    // WinHTTP POST
    std::wstring wide_url(url.begin(), url.end());
    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
        res.error_message = "Failed to parse ACRCloud URL: " + url;
        return res;
    }

    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring object(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.6", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        res.error_message = "WinHttpOpen failed";
        return res;
    }

    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpConnect failed";
        return res;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", object.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpOpenRequest failed";
        return res;
    }

    std::wstring content_type_header = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end());
    WinHttpAddRequestHeaders(hRequest, content_type_header.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpSendRequest failed with error " + std::to_string(err);
        return res;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error_message = "WinHttpReceiveResponse failed with error " + std::to_string(err);
        return res;
    }

    std::string response_text;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        std::vector<char> buf(dwSize + 1);
        if (!WinHttpReadData(hRequest, buf.data(), dwSize, &dwDownloaded)) break;
        buf[dwDownloaded] = 0;
        response_text.append(buf.data(), dwDownloaded);
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    foo_artwork::log_printf("foo_artwork: ACRCloud Response Received (%u bytes)", (unsigned int)response_text.size());

    return parse_response_json(response_text);
}
